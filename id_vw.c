// ID_VW.C

#include "wl_def.h"

// Uncomment the following line, if you get destination out of bounds
// assertion errors and want to ignore them during debugging
//#define IGNORE_BAD_DEST

#ifdef IGNORE_BAD_DEST
#undef assert
#define assert(x) if(!(x)) return
#define assert_ret(x) if(!(x)) return 0
#else
#define assert_ret(x) assert(x)
#endif

#ifdef NOTYET
    //
    // KS: need to find out how to support these
    // with the new code
    //
    #if defined(_arch_dreamcast)
    int      screenWidth = 320;
    int      screenHeight = 200;
    int      screenBits = 8;
    #elif defined(GP2X)
    int      screenWidth = 320;
    int      screenHeight = 240;
        #if defined(GP2X_940)
        int      screenBits = 8;
        #else
        int      screenBits = 16;
        #endif
    #endif
#endif

screen_t screen;
unsigned bordercolor;

pictabletype	*pictable;

int     px,py;
byte	fontcolor,backcolor;
int	    fontnumber;

uint32_t *ylookup;

SDL_Color palette1[256], palette2[256];
SDL_Color curpal[256];


#define CASSERT(x) extern int ASSERT_COMPILE[((x) != 0) * 2 - 1];
#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, SDL_ALPHA_OPAQUE}

SDL_Color gamepal[]={
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

CASSERT(lengthof(gamepal) == 256)

//===========================================================================


/*
=======================
=
= VW_Startup
=
=======================
*/

void VW_Startup (void)
{
    int      x,y;
    int      w,h;
    uint32_t flags = 0;

    if (screen.flags & SC_FULLSCREEN)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
    {
        if (screen.flags & SC_INPUTGRABBED)
            flags |= SDL_WINDOW_INPUT_GRABBED;
    }

#ifndef __EMSCRIPTEN__
    flags |= SDL_WINDOW_RESIZABLE;  // web: the host page sizes the canvas with CSS
#endif
    x = SDL_WINDOWPOS_CENTERED;
    y = SDL_WINDOWPOS_CENTERED;
    w = screen.width;
    h = screen.height;
#ifdef SPEAR
    screen.title = "Spear of Destiny";
#else
    screen.title = "Wolfenstein 3D";
#endif
    screen.window = SDL_CreateWindow(screen.title,x,y,w,h,flags);

    if (!screen.window)
        Quit ("Unable to create window: %s\n",SDL_GetError());

    VW_SetupVideo ();
    VW_InitRndMask ();
}


/*
===================
=
= VW_ClearTexture
=
= Deallocate the rendering texture and its
= associated surfaces
=
===================
*/

void VW_ClearTexture (void)
{
    SDL_DestroyTexture (screen.texture);
    screen.texture = NULL;

    SDL_FreeSurface (screen.surface);
    screen.surface = NULL;

    SDL_FreeSurface (screen.buffer);
    screen.buffer = NULL;

    SafeFree (ylookup);
}


/*
=======================
=
= VW_Shutdown
=
= Deallocate everything
=
=======================
*/

void VW_Shutdown (void)
{
    VW_ClearTexture ();

    SDL_DestroyRenderer (screen.renderer);
    screen.renderer = NULL;

    SDL_DestroyWindow (screen.window);
    screen.window = NULL;
}


/*
=======================
=
= VW_SetupVideo
=
=======================
*/

void VW_SetupVideo (void)
{
    int      i;
    int      w,h;
    uint32_t a,r,g,b;
    uint32_t flags = 0;

    w = screen.width;
    h = screen.height;

    if (!screen.renderer)
    {
        if (!(screen.flags & SC_HWACCEL))
            flags |= SDL_RENDERER_SOFTWARE;
        else
        {
            flags |= SDL_RENDERER_ACCELERATED;

            if (screen.flags & SC_VSYNC)
                flags |= SDL_RENDERER_PRESENTVSYNC;
        }

        screen.renderer = SDL_CreateRenderer(screen.window,-1,flags);

        if (!screen.renderer)
            Quit ("Unable to create renderer: %s\n",SDL_GetError());

        VW_SetViewport (w,h);

        SDL_RenderSetVSync (screen.renderer,(screen.flags & SC_VSYNC) != 0);
    }

    SDL_PixelFormatEnumToMasks (SDL_PIXELFORMAT_ARGB8888,&screen.bits,&r,&g,&b,&a);

    screen.surface = SDL_CreateRGBSurface(0,w,h,screen.bits,r,g,b,a);

    if (!screen.surface)
        Quit ("Unable to create %dx%dx%d surface: %s\n",w,h,screen.bits,SDL_GetError());

    screen.texture = SDL_CreateTexture(screen.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,w,h);

    if (!screen.texture)
        Quit ("Unable to create texture: %s\n",SDL_GetError());

    //
    // create 8 bit screen buffer for drawing
    //
    screen.buffer = SDL_CreateRGBSurface(0,w,h,8,0,0,0,0);

    if (!screen.buffer)
        Quit ("Unable to create screen buffer surface: %s\n",SDL_GetError());

    VW_SetPalette (gamepal,false);

    ylookup = SafeMalloc(screen.height * sizeof(*ylookup));

    for (i = 0; i < screen.height; i++)
        ylookup[i] = i * screen.buffer->pitch;

    screen.scale = w / 320;
    screen.basewidth = w / screen.scale;
    screen.baseheight = h / screen.scale;
    screen.heightoffset = (screen.baseheight % 200) / 2;

    VW_SetBufferOffset (screen.heightoffset);

    SDL_SetWindowMinimumSize (screen.window,screen.basewidth,screen.baseheight);
}


/*
===================
=
= VW_ChangeDisplay
=
===================
*/

void VW_ChangeDisplay (screen_t *scr)
{
    VW_ClearScreen (BLACK);
    VW_UpdateScreen ();

    VW_ChangeWindow (scr);

    if (scr->scale != screen.scale || scr->width != screen.width || scr->height != screen.height)
    {
        //
        // update screen variables and re-allocate everything
        //
        screen.width = scr->width;
        screen.height = scr->height;

        Shutdown3DRenderer ();
        VW_ClearTexture ();

        VW_SetupVideo ();
        VW_InitRndMask ();
        Init3DRenderer ();
    }
}


/*
===================
=
= VW_ChangeWindow
=
= Change the current window resolution and/or
= go to/from fullscreen
=
===================
*/

void VW_ChangeWindow (screen_t *scr)
{
    uint32_t        flags;
    SDL_DisplayMode dm;

    flags = SDL_GetWindowFlags(screen.window);

    if (screen.flags & SC_FULLSCREEN)
    {
        if (scr->scale != screen.scale || scr->width != screen.width || scr->height != screen.height)
        {
            if (SDL_GetWindowDisplayMode(screen.window,&dm))
                Quit ("Unable to get display mode: %s\n",SDL_GetError());

            dm.w = scr->width;
            dm.h = scr->height;

            if (SDL_SetWindowDisplayMode(screen.window,&dm))
                Quit ("Unable to set display mode: %s\n",SDL_GetError());
        }

        if (!(flags & SDL_WINDOW_FULLSCREEN))
        {
            if (SDL_SetWindowFullscreen(screen.window,SDL_WINDOW_FULLSCREEN_DESKTOP))
                Quit ("Unable to set fullscreen mode: %s\n",SDL_GetError());
        }
    }
    else
    {
        if (flags & SDL_WINDOW_FULLSCREEN)
        {
            if (SDL_SetWindowFullscreen(screen.window,0))
                Quit ("Unable to set windowed mode: %s\n",SDL_GetError());
        }

        //
        // KS: there's a weird bug here where switching back from 320x240
        // to 320x200 will not resize the window height - it stays at 240 and
        // scales up the screen. None of the other resolutions do this, but I
        // can't find out why it happens...
        //
        SDL_SetWindowSize (screen.window,scr->width,scr->height);
        SDL_SetWindowPosition (screen.window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
    }

    VW_SetViewport (scr->width,scr->height);
}


/*
=================
=
= VW_SetViewport
=
= If the window resolution is larger than the desktop resolution,
= the viewport dimensions are adjusted to avoid going off the screen
=
=================
*/

void VW_SetViewport (int width, int height)
{
    int             vpwidth,vpheight;
    SDL_Rect        viewport;
    SDL_DisplayMode dm;

    SDL_RenderSetLogicalSize (screen.renderer,width,height);

    if (!(screen.flags & SC_FULLSCREEN))
    {
        if (SDL_GetDesktopDisplayMode(0,&dm))
            Quit ("Unable to get desktop display mode: %s\n",SDL_GetError());

        vpwidth = dm.w;
        vpheight = dm.h;

        if (width > vpwidth || height > vpheight)
        {
            if (width > vpwidth)
            {
                viewport.x = (width - vpwidth) / 2;
                viewport.w = vpwidth;
            }
            else
            {
                viewport.x = 0;
                viewport.w = width;
            }

            if (height > vpheight)
            {
                viewport.y = (height - vpheight) / 2;
                viewport.h = vpheight;
            }
            else
            {
                viewport.y = 0;
                viewport.h = height;
            }

            SDL_RenderSetViewport (screen.renderer,&viewport);

            return;
        }
    }

    SDL_RenderSetViewport (screen.renderer,NULL);
}


/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/

/*
=================
=
= VW_ConvertPalette
=
=================
*/

void VW_ConvertPalette(byte *srcpal, SDL_Color *destpal, int numColors)
{
    int i;

    for(i=0; i<numColors; i++)
    {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
        destpal[i].a = SDL_ALPHA_OPAQUE;
    }
}

/*
=================
=
= VW_FillPalette
=
=================
*/

void VW_FillPalette (int red, int green, int blue)
{
    int i;
    SDL_Color pal[256];

    for(i=0; i<256; i++)
    {
        pal[i].r = red;
        pal[i].g = green;
        pal[i].b = blue;
        pal[i].a = SDL_ALPHA_OPAQUE;
    }

    VW_SetPalette(pal, true);
}

//===========================================================================

/*
=================
=
= VW_GetColor
=
=================
*/

void VW_GetColor	(int color, int *red, int *green, int *blue)
{
    SDL_Color *col = &curpal[color];
    *red = col->r;
    *green = col->g;
    *blue = col->b;
}

//===========================================================================

/*
=================
=
= VW_SetPalette
=
=================
*/

void VW_SetPalette (SDL_Color *palette, bool forceupdate)
{
    memcpy(curpal, palette, sizeof(SDL_Color) * 256);

    if (screen.bits == 8)
        SDL_SetPaletteColors(screen.surface->format->palette, palette, 0, 256);
    else
    {
        SDL_SetPaletteColors(screen.buffer->format->palette, palette, 0, 256);
        if (forceupdate)
            VW_UpdateScreen ();
    }
}


//===========================================================================

/*
=================
=
= VW_GetPalette
=
=================
*/

void VW_GetPalette (SDL_Color *palette)
{
    memcpy(palette, curpal, sizeof(SDL_Color) * 256);
}


//===========================================================================

/*
=================
=
= VW_FadePaletteOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VW_FadePaletteOut (int red, int green, int blue, int steps)
{
	int		    i,j,orig,delta;
	SDL_Color   *origptr, *newptr;

    red = red * 255 / 63;
    green = green * 255 / 63;
    blue = blue * 255 / 63;

	VW_WaitVBL(1);
	VW_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[0];
		newptr = &palette2[0];
		for (j=0;j<256;j++)
		{
			orig = origptr->r;
			delta = red-orig;
			newptr->r = orig + delta * i / steps;
			orig = origptr->g;
			delta = green-orig;
			newptr->g = orig + delta * i / steps;
			orig = origptr->b;
			delta = blue-orig;
			newptr->b = orig + delta * i / steps;
			newptr->a = SDL_ALPHA_OPAQUE;
			origptr++;
			newptr++;
		}

        VW_WaitVBL(1);
		VW_SetPalette (palette2, true);
	}

//
// final color
//
	VW_FillPalette (red,green,blue);

	screen.flags |= SC_FADED;
}


/*
=================
=
= VW_FadePaletteIn
=
=================
*/

void VW_FadePaletteIn (SDL_Color *palette, int steps)
{
	int i,j,delta;

	VW_WaitVBL(1);
	VW_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=0;j<256;j++)
		{
			delta = palette[j].r-palette1[j].r;
			palette2[j].r = palette1[j].r + delta * i / steps;
			delta = palette[j].g-palette1[j].g;
			palette2[j].g = palette1[j].g + delta * i / steps;
			delta = palette[j].b-palette1[j].b;
			palette2[j].b = palette1[j].b + delta * i / steps;
			palette2[j].a = SDL_ALPHA_OPAQUE;
		}

        VW_WaitVBL(1);
		VW_SetPalette(palette2, true);
	}

//
// final color
//
	VW_SetPalette (palette, true);

	screen.flags &= ~SC_FADED;
}


/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

void *VW_LockSurface (SDL_Surface *surface)
{
    if (SDL_MUSTLOCK(surface))
    {
        if (SDL_LockSurface(surface) < 0)
            return NULL;
    }

    return surface->pixels;
}

void VW_UnlockSurface (SDL_Surface *surface)
{
    if (SDL_MUSTLOCK(surface))
        SDL_UnlockSurface (surface);
}


/*
=================
=
= VW_SetBufferOffset
=
= Set the offset in the screen buffer to start drawing
=
= The offset MUST be 0 while in the 3D renderer!
=
=================
*/

void VW_SetBufferOffset (unsigned offset)
{
    offset *= screen.scale;

	assert (offset < screen.height && "VW_SetBufferOffset: Invalid buffer offset!");

    screen.bufferofs = ylookup[offset];
}


/*
=================
=
= VW_Plot
=
=================
*/

void VW_Plot (int x, int y, int color)
{
    VW_Bar (x,y,1,1,color);
}


/*
=================
=
= VW_GetPixel
=
=================
*/

byte VW_GetPixel (int x, int y)
{
    byte *source;
    int  pixel;

    assert_ret(x >= 0 && x < screen.width
            && y >= 0 && y < screen.height
            && "VW_GetPixel: Pixel out of bounds!");

    source = VW_LockSurface(screen.buffer);

    if (source == NULL)
        return 0;

    pixel = source[screen.bufferofs + ylookup[y] + x];

    VW_UnlockSurface (screen.buffer);

    return pixel;
}


/*
=================
=
= VW_Bar
=
=================
*/

void VW_Bar (int x, int y, int width, int height, int color)
{
	byte *dest;

    x *= screen.scale;
    y *= screen.scale;
    width *= screen.scale;
    height *= screen.scale;

	assert (x >= 0 && x + width <= screen.width
            && y >= 0 && y + height <= screen.height
			&& "VW_Bar: Destination rectangle out of bounds!");

	dest = VW_LockSurface(screen.buffer);

	if (dest == NULL)
        return;

	dest += screen.bufferofs + ylookup[y] + x;

	while (height--)
	{
		memset (dest,color,width);

		dest += screen.buffer->pitch;
	}

	VW_UnlockSurface (screen.buffer);
}


void VW_Hlin (int x1, int x2, int y, int color)
{
    VW_Bar (x1,y,x2 - x1 + 1,1,color);
}

void VW_Vlin (int y1, int y2, int x, int color)
{
    VW_Bar (x,y1,1,y2 - y1 + 1,color);
}


/*
============================================================================

							STRING OPS

============================================================================
*/

void VW_DrawPropString (const char *string)
{
	fontstruct  *font;
	int		    width, step, height;
	byte	    *source, *dest;
	byte	    ch;
	int i;
	int sx, sy;

	dest = VW_LockSurface(screen.buffer);
	if(dest == NULL) return;

	font = (fontstruct *)grsegs[STARTFONT + fontnumber];
	height = font->height;
	dest += screen.bufferofs + (screen.scale * (ylookup[py] + px));

	while ((ch = (byte)*string++) != 0)
	{
		width = step = font->width[ch];
		source = ((byte *)font)+font->location[ch];
		while (width--)
		{
			for(i=0; i<height; i++)
			{
				if(source[i*step])
				{
					for(sy=0; sy<screen.scale; sy++)
						for(sx=0; sx<screen.scale; sx++)
							dest[ylookup[screen.scale*i+sy]+sx]=fontcolor;
				}
			}

			source++;
			px++;
			dest+=screen.scale;
		}
	}

	VW_UnlockSurface (screen.buffer);
}


/*
===================
=
= VW_MeasurePropString
=
===================
*/

void VW_MeasurePropString (const char *string, word *width, word *height)
{
    fontstruct *font;

    font = (fontstruct *)grsegs[STARTFONT + fontnumber];

    *height = font->height;

    for (*width = 0; *string; string++)
        *width += font->width[*((byte *)string)];    // proportional width
}


/*
============================================================================

							MEMORY OPS

============================================================================
*/


/*
===================
=
= VW_DePlaneVGA
=
= Unweave a VGA graphic to simplify drawing
=
===================
*/

void VW_DePlaneVGA (byte *source, int width, int height)
{
    int  x,y,plane;
    word size,pwidth;
    byte *temp,*dest,*srcline;

    size = width * height;

    if (width & 3)
        Quit ("DePlaneVGA: width not divisible by 4!");

    temp = SafeMalloc(size);

//
// munge pic into the temp buffer
//
    srcline = source;
    pwidth = width >> 2;

    for (plane = 0; plane < 4; plane++)
    {
        dest = temp;

        for (y = 0; y < height; y++)
        {
            for (x = 0; x < pwidth; x++)
                *(dest + (x << 2) + plane) = *srcline++;

            dest += width;
        }
    }

//
// copy the temp buffer back into the original source
//
    memcpy (source,temp,size);

    SafeFree (temp);
}


void VW_DrawTile8 (int x, int y, int tile)
{
	VW_MemToScreen (grsegs[STARTTILE8]+tile*64,8,8,x,y);
}


void VW_DrawPic (int x, int y, int chunknum)
{
	int	picnum = chunknum - STARTPICS;
	unsigned width,height;

	x &= ~7;

	width = pictable[picnum].width;
	height = pictable[picnum].height;

	VW_MemToScreen (grsegs[chunknum],width,height,x,y);
}


/*
=================
=
= VW_MemToScreen
=
= Draws a block of data to the screen
=
=================
*/

void VW_MemToScreen (byte *source, int width, int height, int x, int y)
{
    byte *dest;
    int color;
    int i,j,sci,scj;
    int m,n;

    x *= screen.scale;
    y *= screen.scale;

    assert (x >= 0 && x + width * screen.scale <= screen.width
            && y >= 0 && y + height * screen.scale <= screen.height
            && "VW_MemToScreen: Destination rectangle out of bounds!");

    dest = VW_LockSurface(screen.buffer);

    if (dest == NULL)
        return;

    dest += screen.bufferofs;

    for (j = 0, scj = 0; j < height; j++, scj += screen.scale)
    {
        for (i = 0, sci = 0; i < width; i++, sci += screen.scale)
        {
            color = source[(j * width) + i];

            for (m = 0; m < screen.scale; m++)
            {
                for (n = 0; n < screen.scale; n++)
                    dest[ylookup[scj + m + y] + sci + n + x] = color;
            }
        }
    }

    VW_UnlockSurface (screen.buffer);
}


/*
=================
=
= VW_SegToScreen
=
= Draws a segment of a block of data to the screen.
= The block has the size srcwidth * height.
= The part at (srcx, srcy) has the size width * height
= and will be drawn to (destx, desty)
=
=================
*/

void VW_SegToScreen (byte *source, int srcwidth, int srcx, int srcy,
                     int destx, int desty, int width, int height)
{
    byte *dest;
    int color;
    int i,j,sci,scj;
    int m,n;

    destx *= screen.scale;
    desty *= screen.scale;

    assert (destx >= 0 && destx + width * screen.scale <= screen.width
            && desty >= 0 && desty + height * screen.scale <= screen.height
            && "VW_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    dest = VW_LockSurface(screen.buffer);

    if (dest == NULL)
        return;

    dest += screen.bufferofs;

    for (j = 0, scj = 0; j < height; j++, scj += screen.scale)
    {
        for (i = 0, sci = 0; i < width; i++, sci += screen.scale)
        {
            color = source[((j + srcy) * srcwidth) + (i + srcx)];

            for (m = 0; m < screen.scale; m++)
            {
                for (n = 0; n < screen.scale; n++)
                    dest[ylookup[scj + m + desty] + sci + n + destx] = color;
            }
        }
    }

    VW_UnlockSurface (screen.buffer);
}


/*
=============================================================================

				Double buffer management routines

=============================================================================
*/

void VW_UpdateScreen (void)
{
	SDL_BlitSurface (screen.buffer,NULL,screen.surface,NULL);

    SDL_UpdateTexture (screen.texture,NULL,screen.surface->pixels,screen.surface->pitch);
    SDL_RenderCopy (screen.renderer,screen.texture,NULL,NULL);
    SDL_RenderPresent (screen.renderer);
}


/*
===================
=
= VW_FizzleFade
=
= returns true if aborted
=
= It uses maximum-length Linear Feedback Shift Registers (LFSR) counters.
= You can find a list of them with lengths from 3 to 168 at:
= http://www.xilinx.com/support/documentation/application_notes/xapp052.pdf
= Many thanks to Xilinx for this list!!!
=
===================
*/

// XOR masks for the pseudo-random number sequence starting with n=17 bits
static const uint32_t rndmasks[] = {
                    // n    XNOR from (starting at 1, not 0 as usual)
    0x00012000,     // 17   17,14
    0x00020400,     // 18   18,11
    0x00040023,     // 19   19,6,2,1
    0x00090000,     // 20   20,17
    0x00140000,     // 21   21,19
    0x00300000,     // 22   22,21
    0x00420000,     // 23   23,18
    0x00e10000,     // 24   24,23,22,17
    0x01200000,     // 25   25,22      (this is enough for 8191x4095)
};

static unsigned int rndbits_y;
static unsigned int rndmask;

extern SDL_Color curpal[256];

// Returns the number of bits needed to represent the given value
static int log2_ceil(uint32_t x)
{
    int n = 0;
    uint32_t v = 1;
    while(v < x)
    {
        n++;
        v <<= 1;
    }
    return n;
}

void VW_InitRndMask (void)
{
    int rndbits_x = log2_ceil(screen.width);
    rndbits_y = log2_ceil(screen.height);

    int rndbits = rndbits_x + rndbits_y;
    if(rndbits < 17)
        rndbits = 17;       // no problem, just a bit slower
    else if(rndbits > 25)
        rndbits = 25;       // fizzle fade will not fill whole screen

    rndmask = rndmasks[rndbits - 17];
}

boolean VW_FizzleFade (int x1, int y1, int width, int height, int frames, boolean abortable)
{
    unsigned x, y, p, frame, pixperframe;
    int32_t  rndval;

    x1 *= screen.scale;
    y1 *= screen.scale;
    width *= screen.scale;
    height *= screen.scale;

    rndval = 1;
    pixperframe = width * height / frames;

    IN_StartAck ();

    frame = GetTimeCount();
    byte *srcptr = VW_LockSurface(screen.buffer);
    if(srcptr == NULL) return false;

    while (1)
    {
        IN_ProcessEvents();

        if(abortable && IN_CheckAck ())
        {
            VW_UnlockSurface(screen.buffer);
            VW_UpdateScreen ();
            return true;
        }

        byte *destptr = VW_LockSurface(screen.surface);

        if (!destptr)
            Quit ("Unable to lock dest surface: %s\n",SDL_GetError());

        for (p = 0; p < pixperframe; p++)
        {
            //
            // seperate random value into x/y pair
            //
            x = rndval >> rndbits_y;
            y = rndval & ((1 << rndbits_y) - 1);

            //
            // advance to next random element
            //
            rndval = (rndval >> 1) ^ (rndval & 1 ? 0 : rndmask);

            if (x >= width || y >= height)
                p--;                         // not into the view area; get a new pair
            else
            {
                //
                // copy one pixel
                //
                if (screen.bits == 8)
                {
                    *(destptr + (y1 + y) * screen.surface->pitch + x1 + x)
                        = *(srcptr + (y1 + y) * screen.buffer->pitch + x1 + x);
                }
                else
                {
                    byte col = *(srcptr + (y1 + y) * screen.buffer->pitch + x1 + x);
                    uint32_t fullcol = SDL_MapRGBA(screen.surface->format, curpal[col].r, curpal[col].g, curpal[col].b,SDL_ALPHA_OPAQUE);
                    memcpy(destptr + (y1 + y) * screen.surface->pitch + (x1 + x) * screen.surface->format->BytesPerPixel,
                        &fullcol, screen.surface->format->BytesPerPixel);
                }
            }

            if (rndval == 1)
            {
                //
                // entire sequence has been completed
                //
                VW_UnlockSurface (screen.buffer);
                VW_UnlockSurface (screen.surface);
                VW_UpdateScreen ();

                return false;
            }
        }

        VW_UnlockSurface (screen.surface);

        SDL_UpdateTexture (screen.texture,NULL,screen.surface->pixels,screen.surface->pitch);
        SDL_RenderCopy (screen.renderer,screen.texture,NULL,NULL);
        SDL_RenderPresent (screen.renderer);

        frame++;
        Delay(frame - GetTimeCount());        // don't go too fast
    }

    return false;
}

