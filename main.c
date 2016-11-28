/*

All capture board interface and capture code provided from
http://3dscapture.com/ds/index.html

I just wrote the SDL rendering portion. I need to find out what
their license is for using the linux sample code for capturing.

*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <memory.h>
#include <libusb-1.0/libusb.h>
#include <SDL2/SDL.h>

bool dscapture_init();
void dscapture_deinit();
bool dscapture_grabFrame(uint16_t *frameBuf);

enum {
    USBDS_VID               = 0x16D0,
    USBDS_PID               = 0x0647,
    DEFAULT_CONFIGURATION   = 1,
    CAPTURE_INTERFACE       = 0,
    CONTROL_TIMEOUT         = 500,
    BULK_TIMEOUT            = 500,
    EP2_IN                  = 2 | LIBUSB_ENDPOINT_IN,
    CMDIN_STATUS            = 0x31,
    CMDIN_FRAMEINFO         = 0x30,
    CMDOUT_CAPTURE_START    = 0x30,
    CMDOUT_CAPTURE_STOP     = 0x31
};

enum { lcdWidth = 256, lcdHeight = 192 };

uint16_t buf16[lcdWidth * lcdHeight * 2];
uint8_t buf24[lcdWidth * lcdHeight * 2 * 3];

static libusb_device_handle *dev = NULL;
static bool init = false;

bool dscapture_init() {
    if(libusb_init(NULL) != LIBUSB_SUCCESS) {
        printf("Could not initialize lib usb.\n");
        return false;
    }
        
    init = true;

    dev = libusb_open_device_with_vid_pid(NULL, USBDS_VID, USBDS_PID);
    if(!dev) {
        printf("Could not open device.\n");
        goto err;
    }
    
    if(libusb_set_configuration(dev, DEFAULT_CONFIGURATION) != LIBUSB_SUCCESS)
        goto err;
    
    if(libusb_claim_interface(dev, CAPTURE_INTERFACE) != LIBUSB_SUCCESS)
        goto err;
    
    return true;

err:
    dscapture_deinit();
    return false;
}

void dscapture_deinit() {
    if(dev) {
        libusb_release_interface(dev, CAPTURE_INTERFACE);
        libusb_close(dev);
        dev = NULL;
    }
    if(init) {
        libusb_exit(NULL);
        init = false;
    }
}

static int vend_in(uint8_t bRequest, uint16_t wLength, uint8_t *buf) {
    return libusb_control_transfer(dev, (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN), bRequest, 0, 0, buf, wLength, CONTROL_TIMEOUT);
}

static int vend_out(uint8_t bRequest, uint16_t wValue, uint16_t wLength, uint8_t *buf) {
    return libusb_control_transfer(dev, (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT), bRequest, wValue, 0, buf, wLength, CONTROL_TIMEOUT);
}

static int bulk_in(uint8_t *buf, int length, int *transferred) {
    return libusb_bulk_transfer(dev, EP2_IN, buf, length, transferred, BULK_TIMEOUT);
}

bool dscapture_grabFrame(uint16_t *frameBuf) {
    enum {
        infoSize = 64,
        lcdWidth = 256,
        lcdHeight = 192,
        frameSize = 1024 * lcdHeight,
    };

    static uint16_t tmpBuf[frameSize / sizeof(uint16_t)];
    static uint8_t frameInfo[infoSize];

    uint8_t dummy;
    if(vend_out(CMDOUT_CAPTURE_START, 0, 0, &dummy) < 0)
        return false;

    int transferred;
    int result;
    int bytesIn = 0;
    uint8_t *p = (uint8_t*)tmpBuf;
    
    do {
        result = bulk_in(p, frameSize-bytesIn, &transferred);
        if(result == LIBUSB_SUCCESS) {
            bytesIn += transferred;
            p += transferred;
        }
    } while(bytesIn < frameSize && result == LIBUSB_SUCCESS && transferred > 0);

    if(result != LIBUSB_SUCCESS)
        return false;
    if(vend_in(CMDIN_FRAMEINFO, infoSize, frameInfo) < 0)
        return false;
    if((frameInfo[0] & 3) != 3)
        return false;
    if(!frameInfo[52])
        return false;

    int i;
    int line;
    uint16_t *src = tmpBuf;
    uint16_t *dst = frameBuf;

    for(line=0; line < lcdHeight * 2; ++line) {
        if(frameInfo[line >> 3] & (1 << (line & 7))) {
            for(i=0; i < lcdWidth / 2; ++i) {
                dst[0] = src[1];
                dst[lcdWidth * lcdHeight] = src[0];
                dst++;
                src += 2;
            }
        } else {
            memcpy(dst, dst - 256, 256);
            memcpy(dst + 256 * 192, dst + 256 * 191, 256);
            dst += 128;
        }
    }
    
    return true;
}

static void BGR16toRGB24(uint8_t *out, uint16_t *in) {
    int i;
    for(i=0;i<lcdWidth*lcdHeight*2;i++) {
        unsigned char r,g,b;
        g=((*in)>>5)&0x3f;
        b=((*in<<1)&0x3e)|(g&1);
        r=(((*in)>>10)&0x3e)|(g&1);
        out[0]=(r<<2) | (r>>4);
        out[1]=(g<<2) | (g>>4);
        out[2]=(b<<2) | (b>>4);
        out+=3;
        in++;
    }
}

int main(int argc, char **argv) {
	if(argc != 4) {
		printf("Usage: %s width height screens\n", argv[0]);
		printf("Example: %s 800 600 2\n", argv[0]);
		return 0;
	}
	
	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	int screens = atoi(argv[3]);
	
	if(screens != 1 && screens != 2) {
		screens = 1; // default
	}
	
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Could not initialize SDL.\n");
        goto end;
    }
    
    SDL_Window *window = SDL_CreateWindow("DS Stream",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height,
        SDL_WINDOW_RESIZABLE);
        
    if(!window) {
        printf("Could not create window.\n");
        goto end;
    }
    
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    
    if(!renderer) {
        printf("Could not create renderer.\n");
        goto end;
    }
    
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING, 256, 192 * screens);

    if(!texture) {
        printf("Could not create streaming texture.\n");
        goto end;
    }

    if(!dscapture_init()) {
        printf("Could not initialize DS capture card.\n");
        goto end;
    }
    
    void *pixels;
    int pitch;
    bool done = false;
    
    SDL_RenderSetLogicalSize(renderer, width, height);
    
    while(!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    if(event.key.keysym.sym == SDLK_ESCAPE) {
                        done = true;
                    }
                    break;
                case SDL_QUIT:
                    done = true;
                    break;
            }
        }
            
        if(SDL_LockTexture(texture, NULL, &pixels, &pitch) < 0) {
            printf("Could not lock texture.\n");
            break;
        }
        
        dscapture_grabFrame(buf16);
        BGR16toRGB24(buf24, buf16);
        SDL_memcpy(pixels, buf24, 256 * 192 * 3 * screens);
        SDL_UnlockTexture(texture);
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    
end:
    dscapture_deinit();
    SDL_DestroyRenderer(renderer);
    return 0;
}
