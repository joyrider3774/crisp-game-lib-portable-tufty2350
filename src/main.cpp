#include "powman.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <cstring>
#include <malloc.h>
#include <float.h>
#include <math.h>
#include "common/pimoroni_bus.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "glcdfont.h"
#include "lib/menu.h"
#include "lib/machineDependent.h"
#include "lib/menuGameList.h"
#include "lib/particle.h"
#include "lib/random.h"
#include "lib/sound.h"
#include "lib/textPattern.h"
#include "lib/vector.h"
#include "lib/cglp.h"

using namespace pimoroni;

#define BUTTON_A_MASK (1<<0)
#define BUTTON_B_MASK (1<<1)
#define BUTTON_C_MASK (1<<2)
#define BUTTON_UP_MASK (1<<3)
#define BUTTON_DOWN_MASK (1<<4)
#define BUTTON_HOME_MASK (1<<5)

#define COLOR_SWAP(c) ((uint16_t)((((c) >> 8) | (c) << 8)))
#define COLOR(r,g,b) COLOR_SWAP(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))



ParallelPins parallel_bus = {
    .cs = 27,
    .dc = 28,
    .wr_sck = 30,
    .rd_sck = 31,
    .d0 = 32,
    .bl = 26
};

ST7789 *st7789;
PicoGraphics_PenRGB565 *graphics;
uint16_t *fb;
static int8_t vol = 3;
static unsigned char clearColorR = 0;
static unsigned char clearColorG = 0;
static unsigned char clearColorB = 0;
const uint16_t timePerFrame =  1000000 / FPS; 
static float frameRate = 0;
static uint64_t currentTime = 0, lastTime = 0, frameTime = 0;
static int WINDOW_WIDTH = 0;
static int WINDOW_HEIGHT = 0;
static float scale = 1.0f;
static int viewW = 0;
static int viewH = 0;
static int origViewW = 0;
static int origViewH = 0;
static int offsetX = 0;
static int offsetY = 0;
static float wscale = 1.0f;
static int debugMode = 0;
static float mouseX = 0, mouseY = 0;
static float prevRealMouseX = 0, prevRealMouseY = 0;
static int debounce = 0;
static int clipx = 0;
static int clipy = 0;
static int clipw = 0;
static int cliph = 0;
static bool endFrame = true;
static int screenOffsetX = 0;
static int screenOffsetY = 0;


uint8_t readButtons()
{
    uint8_t ret = 0;
    if (!gpio_get(BW_SWITCH_A))
        ret |= BUTTON_A_MASK;
    if (!gpio_get(BW_SWITCH_B))
        ret |= BUTTON_B_MASK;
    if (!gpio_get(BW_SWITCH_C))
        ret |= BUTTON_C_MASK;

    if (!gpio_get(BW_SWITCH_UP))
        ret |= BUTTON_UP_MASK;
    if (!gpio_get(BW_SWITCH_DOWN))
        ret |= BUTTON_DOWN_MASK;
    if (!gpio_get(BW_SWITCH_HOME))
        ret |= BUTTON_HOME_MASK;
    return ret;
}

inline void setClipRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    clipx = x < 0 ? 0 : x;
    clipy = y < 0 ? 0 : y;
    
    int16_t right = x + w;
    int16_t bottom = y + h;
    
    right = right > WINDOW_WIDTH ? WINDOW_WIDTH : right;
    bottom = bottom > WINDOW_HEIGHT ? WINDOW_HEIGHT : bottom;
    
    clipw = right - clipx;   // Width from clipped left to clipped right
    cliph = bottom - clipy;  // Height from clipped top to clipped bottom
    
    // Ensure non-negative dimensions
    if (clipw < 0) clipw = 0;
    if (cliph < 0) cliph = 0;
}

inline void memset16(uint16_t *dest, uint16_t value, size_t num_words) {
    size_t i;
    for (i = 0; i < num_words; i++) {
        dest[i] = value;
    }
}

inline void fillRectBuffer(uint16_t *buffer, uint16_t stride, int16_t clipx, int16_t clipy, int16_t clipw, int16_t cliph, float posx, float posy, float w, float h, uint16_t col)
{
    // Fast early exits
    if (w <= 0 || h <= 0 || !buffer) return;
    
    const int16_t clip_right = clipx + clipw;
    const int16_t clip_bottom = clipy + cliph;
    const int16_t rect_right = posx + w;
    const int16_t rect_bottom = posy + h;
    
    const int16_t minx = posx > clipx ? posx : clipx;
    const int16_t miny = posy > clipy ? posy : clipy;
    const int16_t maxx = rect_right < clip_right ? rect_right : clip_right;
    const int16_t maxy = rect_bottom < clip_bottom ? rect_bottom : clip_bottom;
    
    const int16_t fill_width = maxx - minx;
    const int16_t fill_height = maxy - miny;
    if (fill_width <= 0 || fill_height <= 0) return;
    
    const uint32_t col32 = ((uint32_t)col << 16) | col;
    
    // for (int y = miny; y< maxy; y++)
    //     memset16(&buffer[y*stride + minx], col, fill_width);
    // return;

    // Single scanline optimization - use memset if beneficial
    if (fill_height == 1) {
        uint16_t* ptr = &buffer[miny * stride + minx];
        
        // For small fills, direct writes are faster than memset overhead
        if (fill_width <= 4) {
            for (int16_t i = 0; i < fill_width; i++) {
                *ptr++ = col;
            }
            return;
        }
        
        // Handle alignment
        if ((uintptr_t)ptr & 2) {
            *ptr++ = col;
            int16_t remaining = fill_width - 1;
            
            // Write 32-bit pairs
            uint32_t* ptr32 = (uint32_t*)ptr;
            while (remaining >= 2) {
                *ptr32++ = col32;
                remaining -= 2;
            }
            if (remaining) {
                *((uint16_t*)ptr32) = col;
            }
        } else {
            // Aligned path
            uint32_t* ptr32 = (uint32_t*)ptr;
            int16_t pairs = fill_width >> 1;
            
            while (pairs >= 4) {
                *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32;
                pairs -= 4;
            }
            while (pairs--) {
                *ptr32++ = col32;
            }
            if (fill_width & 1) {
                *((uint16_t*)ptr32) = col;
            }
        }
        return;
    }
    
    // Multi-scanline: Optimize for common case (aligned, even width)
    if (!(minx & 1) && !(fill_width & 1)) {
        // Fully aligned fast path
        for (int16_t y = miny; y < maxy; y++) {
            uint32_t* ptr32 = (uint32_t*)&buffer[y * stride + minx];
            int16_t pairs = fill_width >> 1;
            
            while (pairs >= 4) {
                *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32;
                pairs -= 4;
            }
            while (pairs--) {
                *ptr32++ = col32;
            }
        }
        return;
    }
    
    // General case (unaligned or odd width)
    for (int16_t y = miny; y < maxy; y++) {
        uint16_t* ptr = &buffer[y * stride + minx]; 
        int16_t remaining = fill_width;
        
        // Handle alignment
        if ((uintptr_t)ptr & 2) {
            *ptr++ = col;
            remaining--;
        }
        
        // Write pairs of pixels
        uint32_t* ptr32 = (uint32_t*)ptr;
        while (remaining >= 8) {
            *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32; *ptr32++ = col32;
            remaining -= 8;
        }
        while (remaining >= 2) {
            *ptr32++ = col32;
            remaining -= 2;
        }
        
        if (remaining) {
            *((uint16_t*)ptr32) = col;
        }
    }
}

static void loadHighScores()
{
    //read_save(saveData);
}

static void saveHighScores()
{
//    write_save(saveData);
}

void md_playTone(float freq, float duration, float when)
{ 
    
}

void md_stopTone()
{

}

float md_getAudioTime()
{
    return (time_us_32()  / 1000.0);
}
    
void md_drawCharacter(unsigned char grid[CHARACTER_HEIGHT][CHARACTER_WIDTH][3],
                     float x, float y, int hash) 
{
    unsigned char r,g,b;
    for (int yy = 0; yy < CHARACTER_HEIGHT; yy++) {
        for (int xx = 0; xx < CHARACTER_WIDTH; xx++) {
            r = grid[yy][xx][0];
            g = grid[yy][xx][1];
            b = grid[yy][xx][2];
            
            if ((r == 0) && (g == 0) && (b == 0)) continue;

            fillRectBuffer(fb, WINDOW_WIDTH,clipx,clipy,clipw,cliph,
                (offsetX + x*scale + (float)xx * scale),
                (offsetY + y*scale + (float)yy * scale),
                ceilf(scale),
                ceilf(scale),
                COLOR(r, g, b)
            );
        }
    }
}

 void md_drawRect(float x, float y, float w, float h, unsigned char r,
                  unsigned char g, unsigned char b) {
    //adjust for different behaviour between sdl and js in case of negative width / height
    if(w < 0.0f) {
        x += w;
        w *= -1.0f;
    }
    if(h < 0.0f) {
        y += h;
        h *= -1.0f;
    }

    fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, 
        (int16_t)(offsetX + x * scale), 
        (int16_t)(offsetY + y * scale),
        (int16_t)ceilf(w * scale),
        (int16_t)ceilf(h * scale),
        COLOR(r, g, b)
    );
}

void md_clearView(unsigned char r, unsigned char g, unsigned char b) 
{
    setClipRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COLOR(clearColorR,clearColorG,clearColorB));
    setClipRect(offsetX, offsetY, viewW, viewH);

    fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, 
        offsetX,
        offsetY,
        viewW,
        viewH,
        COLOR(r, g, b)
    );
}

void md_clearScreen(unsigned char r, unsigned char g, unsigned char b)
{
    clearColorR = r;
    clearColorG = g;
    clearColorB = b;
    //window width & height can be smaller than actual screen width & height if hardcoded
    setClipRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COLOR(r,g,b));
    setClipRect(offsetX, offsetY, viewW, viewH);
}

void md_initView(int w, int h) 
{   

    WINDOW_WIDTH = st7789->width;
    WINDOW_HEIGHT = st7789->height;
    screenOffsetX = (st7789->width -WINDOW_WIDTH) >> 1;
    screenOffsetY = (st7789->height -WINDOW_HEIGHT) >> 1;
    
    float wscalex = (float)WINDOW_WIDTH / (float)st7789->width;
    float wscaley = (float)WINDOW_HEIGHT / (float)st7789->height;
    wscale = (wscaley < wscalex) ? wscaley : wscalex;

    origViewW = w;
    origViewH = h;
    float xScale = (float)WINDOW_WIDTH / w;
    float yScale = (float)WINDOW_HEIGHT / h;
    if (yScale < xScale)
        scale = yScale;
    else
        scale = xScale;
    viewW = (int)floorf((float)w * scale);
    viewH = (int)floorf((float)h * scale);
    offsetX = (int)(WINDOW_WIDTH - viewW) >> 1;
    offsetY = (int)(WINDOW_HEIGHT - viewH) >> 1;
    mouseX = viewW >> 1;
    mouseY = viewH >> 1; 
    setMousePos(offsetX + mouseX, offsetY + mouseY);
    setClipRect(offsetX, offsetY, viewW, viewH);
}

void md_consoleLog(char* msg) 
{ 
    const char* tmp = msg;
    printf("%s", tmp); 
}

// Draw a single pixel
inline void bufferDrawPixel(uint16_t* fb, int16_t stride, int16_t x, int16_t y, uint16_t color) {
    if (!fb) return;
    if(x >= 0 && x < WINDOW_WIDTH && y >= 0 && y < WINDOW_HEIGHT)
        *(fb + y * stride + x) = color;
}

// Draw a character
void bufferDrawChar(uint16_t* fb, int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size, const uint8_t* font) {
    if (!fb || !font) return;
    
    if (c >= 176) c++;
    
    for (int8_t i = 0; i < 5; i++) {
        uint8_t line = font[c * 5 + i];
        
        for (int8_t j = 0; j < 8; j++) {
            if (line & 0x1) {
                if (size == 1) {
                    bufferDrawPixel(fb, WINDOW_WIDTH, x + i, y + j, color);
                } else {
                    fillRectBuffer(fb,WINDOW_WIDTH, 0,0,WINDOW_WIDTH,WINDOW_HEIGHT, x + i * size, y + j * size, size, size, color);
                }
            } else if (bg != color) {
                if (size == 1) {
                    bufferDrawPixel(fb, WINDOW_WIDTH, x + i, y + j, bg);
                } else {
                    fillRectBuffer(fb,WINDOW_WIDTH, 0,0,WINDOW_WIDTH,WINDOW_HEIGHT, x + i * size, y + j * size, size, size, bg);
                }
            }
            line >>= 1;
        }
    }
    
    if (bg != color) {
        if (size == 1) {
            for (int8_t j = 0; j < 8; j++) {
                bufferDrawPixel(fb, WINDOW_WIDTH, x + 5, y + j, bg);
            }
        } else {
            fillRectBuffer(fb, WINDOW_WIDTH,  0,0,WINDOW_WIDTH,WINDOW_HEIGHT, x + 5 * size, y, size, 8 * size, bg);
        }
    }
}

// Print a string
void bufferPrint(uint16_t* fb, int16_t x, int16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size, const uint8_t* font) {
    if (!fb || !str || !font) return;
    
    int16_t cursorX = x;
    
    while (*str) {
        bufferDrawChar(fb, cursorX, y, *str, color, bg, size, font);
        cursorX += (6 * size);
        str++;
    }
}


void printDebugCpuRamLoad()
{
    if(debugMode)
    {
        int currentFPS = (int)frameRate;
        char debuginfo[80];
        
        int fps_int = (int)frameRate;
        int fps_frac = (int)((frameRate - fps_int) * 100);
        sprintf(debuginfo, "F:%3d.%2d", fps_int, fps_frac);
        bufferPrint(fb, 0, 0, debuginfo, 0xFFFF,  0x0000, 1, font);
    }
}

int main() {
    stdio_init_all();
    st7789 = new ST7789(320, 240, ROTATE_180, parallel_bus);
    graphics = new PicoGraphics_PenRGB565(st7789->width, st7789->height, nullptr);
    graphics->color = 0x0000;
    graphics->clear();
    st7789->update(graphics);
    st7789->set_backlight(200);
    uint8_t pressed_buttons = readButtons();
    pressed_buttons = readButtons();
    if(pressed_buttons & BUTTON_UP_MASK)
        debugMode = true;
    fb = (uint16_t*)graphics->frame_buffer;

	initGame();
    loadHighScores();
	
    currentTime = time_us_64();
    lastTime = 0;
    while (true) 
    {
        currentTime = time_us_64();
        frameTime  = currentTime - lastTime;  
        if((frameTime < timePerFrame) || !endFrame)
            continue;
        endFrame = false;
        frameRate = 1000000.0 / frameTime;
        lastTime = currentTime;

		bool mouseUsed = getGame(currentGameIndex).usesMouse;
		pressed_buttons = readButtons();
		
		setButtonState(
			!mouseUsed && (pressed_buttons & BUTTON_A_MASK), 
			!mouseUsed && (pressed_buttons & BUTTON_C_MASK), 
			!mouseUsed && (pressed_buttons & BUTTON_UP_MASK), 
			!mouseUsed && (pressed_buttons & BUTTON_DOWN_MASK), 
			(pressed_buttons & BUTTON_HOME_MASK), 
			(pressed_buttons & BUTTON_B_MASK) 
		); 

		if (mouseUsed)
		{
			if(pressed_buttons & BUTTON_C_MASK)
				mouseX += WINDOW_WIDTH /100.0f;
			
			if(pressed_buttons & BUTTON_A_MASK)
				mouseX -= WINDOW_WIDTH /100.0f;
				
			if(pressed_buttons & BUTTON_UP_MASK)
				mouseY -= WINDOW_HEIGHT /100.0f;
		
			if(pressed_buttons & BUTTON_DOWN_MASK)
				mouseY += WINDOW_HEIGHT /100.0f;

			mouseX = clamp(mouseX, 0, WINDOW_WIDTH - 2*offsetX -1);
			mouseY = clamp(mouseY, 0, WINDOW_HEIGHT - 2*offsetY -1);

			setMousePos(mouseX / scale, mouseY / scale);
		}

		updateFrame();
		

		if(mouseUsed && !isInGameOver)
		{
			uint16_t col = COLOR(255, 105, 180);
			fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, offsetX + (mouseX-3*wscale), offsetY + (mouseY-1*wscale), (7.0f*wscale),(3.0f*wscale),col);
			fillRectBuffer(fb, WINDOW_WIDTH,clipx, clipy, clipw, cliph, offsetX + (mouseX-1*wscale), offsetY + (mouseY-3*wscale), (3.0f*wscale),(7.0f*wscale),col);
		}

		if(!isInMenu)
			if(pressed_buttons & BUTTON_HOME_MASK)
				goToMenu();
		
		printDebugCpuRamLoad();
		st7789->update(graphics);
		endFrame = true;
	}
    return 0;
}