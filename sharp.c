#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>

#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/timer.h>

#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <linux/gpio.h>
#include <linux/uaccess.h>

#define LCDWIDTH 400
#define VIDEOMEMSIZE    (1*1024*1024)   /* 1 MB */

/*  Modifying to work with JDI LPM027M128C 8 color display 
    It is pin compatible with the sharp display. 
    Referencing JDI_MIP_Display.cpp for hints on
    what needs to be changed.*/

//char commandByte = 0b10000000; // single line 3 bit mode
char commandByte = 0b10010000; // single line 4 bit mode
// char vcomByte    = 0b01000000;
char clearByte   = 0b00100000;
char paddingByte = 0b00000000;

// char DISP       = 22;
// char SCS        = 8;
// char VCOM       = 23;
char DISP       = 24;
char SCS        = 23;
char VCOM       = 25;

int lcdWidth = LCDWIDTH;
int lcdHeight = 240;
int fpsCounter;

static int seuil = 4; // Indispensable pour fbcon
module_param(seuil, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP );

char vcomState;

// unsigned char lineBuffer[3*LCDWIDTH/8];

struct sharp {
    struct spi_device	*spi;
	int			        id;
    char			    name[sizeof("sharp-3")];
    struct mutex		mutex;
	struct work_struct	work;
	spinlock_t		    lock; 
};

struct sharp   *screen;
struct fb_info *info;
// struct vfb_data *vdata;

static void *videomemory;
static u_long videomemorysize = VIDEOMEMSIZE;

void vfb_fillrect(struct fb_info *p, const struct fb_fillrect *region);
// static int vfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green, unsigned int blue, unsigned int transp, struct fb_info *info);
static int vfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
void sendLine(char *buffer, char lineNumber);

static struct fb_var_screeninfo vfb_default = {
    .xres =     400,
    .yres =     240,
    .xres_virtual = 400,
    .yres_virtual = 240,
    .bits_per_pixel = 8,
    .grayscale = 0,
    .red =      { 0, 3, 0 },
    .green =    { 3, 3, 0 },
    .blue =     { 6, 2, 0 }, 
    .activate = FB_ACTIVATE_NOW,
    .height =   400,
    .width =    240,
    .pixclock = 20000,
    .left_margin =  0,
    .right_margin = 0,
    .upper_margin = 0,
    .lower_margin = 0,
    .hsync_len =    128,
    .vsync_len =    128,
    .vmode =    FB_VMODE_NONINTERLACED,
    };

static struct fb_fix_screeninfo vfb_fix = {
    .id =       "Sharp FB",
    .type =     FB_TYPE_PACKED_PIXELS,
    .line_length = 400,
    .xpanstep = 0,
    .ypanstep = 0,
    .ywrapstep =    0,
    .visual =	FB_VISUAL_PSEUDOCOLOR,
    // TODO: see if we can use hw acceleration at all for pi zero.
    .accel =    FB_ACCEL_NONE,
};

static struct fb_ops vfb_ops = {
    .fb_read        = fb_sys_read,
    .fb_write       = fb_sys_write,
    .fb_fillrect    = sys_fillrect,
    .fb_copyarea    = sys_copyarea,
    .fb_imageblit   = sys_imageblit,
    // .fb_image = sys_image,
    .fb_mmap    = vfb_mmap,
    // .fb_setcolreg   = vfb_setcolreg,
};

// struct vfb_data {
//     u32 palette[8]; // Array to store color palette entries
//     // Add other driver-specific data members as needed
// };

static struct task_struct *thread1;
static struct task_struct *fpsThread;
static struct task_struct *vcomToggleThread;

// static int vfb_setcolreg(unsigned int regno, unsigned int red, unsigned int green, unsigned int blue, unsigned int transp, struct fb_info *info)
// {
//     struct vfb_data *vdata = info->par; // Assuming you have a structure called vfb_data to hold your driver-specific data
    
//     if (regno >= 8)
//         return -EINVAL; // Invalid color palette index
    
//     // Assuming your display uses 3 bits per color component (bits_per_pixel = 3)
//     vdata->palette[regno] = ((red & 0x7) << 5) | ((green & 0x7) << 2) | (blue & 0x3);

//     return 0;
// }

static int vfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    unsigned long start = vma->vm_start;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long page, pos;
    printk(KERN_CRIT "start %ld size %ld offset %ld", start, size, offset);

    if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
        return -EINVAL;
    if (size > info->fix.smem_len)
        return -EINVAL;
    if (offset > info->fix.smem_len - size)
        return -EINVAL;

    pos = (unsigned long)info->fix.smem_start + offset;

    while (size > 0) {
        page = vmalloc_to_pfn((void *)pos);
        if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
            return -EAGAIN;
        }
        start += PAGE_SIZE;
        pos += PAGE_SIZE;
        if (size > PAGE_SIZE)
            size -= PAGE_SIZE;
        else
            size = 0;
    }

    return 0;
}

void vfb_fillrect(struct fb_info *p, const struct fb_fillrect *region)
{
    printk(KERN_CRIT "from fillrect");
}

static void *rvmalloc(unsigned long size)
{
    void *mem;
    unsigned long adr;  /* Address of the allocated memory */

    size = PAGE_ALIGN(size);
    mem = vmalloc_32(size);
    if (!mem)
        return NULL;

    memset(mem, 0, size); /* Clear the ram out, no junk to the user */
    adr = (unsigned long) mem;
    while (size > 0) {
        SetPageReserved(vmalloc_to_page((void *)adr));
        adr += PAGE_SIZE;
        size -= PAGE_SIZE;
    }

    return mem;
}

static void rvfree(void *mem, unsigned long size)
{
    unsigned long adr;

    if (!mem)
        return;

    adr = (unsigned long) mem;
    while ((long) size > 0) {
        ClearPageReserved(vmalloc_to_page((void *)adr));
        adr += PAGE_SIZE;
        size -= PAGE_SIZE;
    }
    vfree(mem);
}

void clearDisplay(void) {
    char buffer[2] = {clearByte, paddingByte};
    gpio_set_value(SCS, 1);

    spi_write(screen->spi, (const u8 *)buffer, 2);

    gpio_set_value(SCS, 0);
}

void colorCorners(unsigned char *screenBuffer) {
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 200; x++) {
            if (x < 10 && y < 20) {
                screenBuffer[y * 204 + x + 2] = 238; // white 11101110 => 238
            } else if (x > 190 && y < 20) {
                screenBuffer[y * 204 + x + 2] = 136; // red   10001000 => 136
            } else if (x < 10 && y > 220) {
                screenBuffer[y * 204 + x + 2] = 68;  // green 01000100 =>  68
            } else if (x > 190 && y > 220) {
                screenBuffer[y * 204 + x + 2] = 34;  // blue  00100010 =>  34
            }
        }
        if (y < 20 || y > 220) {
            gpio_set_value(SCS, 1);
            spi_write(screen->spi, (const u8 *)(screenBuffer+(y*204)), 204);
            gpio_set_value(SCS, 0);
        }
    }
}

void colorBar(unsigned char *screenBuffer) {
    for(int y = 0; y < 40; y++) {
        for(int x = 0; x < 160; x++) {
            if (x < 20) {
                screenBuffer[y * 204 + x + 2] = 0b00000000; //black
            } else if (x < 40) {
                screenBuffer[y * 204 + x + 2] = 0b10001000; // red
            } else if (x < 60) {
                screenBuffer[y * 204 + x + 2] = 0b01000100; // green 
            } else if (x < 80) {
                screenBuffer[y * 204 + x + 2] = 0b00100010; // blue
            } else if (x < 100) {
                screenBuffer[y * 204 + x + 2] = 0b11001100; // red + green => yellow
            } else if (x < 120) {
                screenBuffer[y * 204 + x + 2] = 0b01100110; // green + blue => cyan
            } else if (x < 140) {
                screenBuffer[y * 204 + x + 2] = 0b10101010; // red + blue => magenta
            } else if (x < 160) {
                screenBuffer[y * 204 + x + 2] = 0b11101110; // red + green + blue => white
            } 
        }
        gpio_set_value(SCS, 1);
        spi_write(screen->spi, (const u8 *)(screenBuffer+(y*204)), 204);
        gpio_set_value(SCS, 0);
    }
}

// char reverseByte(char b) {
//   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
//   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
//   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
//   return b;
// }

int vcomToggleFunction(void* v) 
{
    while (!kthread_should_stop()) 
    {
        msleep(50);
        vcomState = vcomState ? 0:1;
        gpio_set_value(VCOM, vcomState);
    }
    return 0;
}

int fpsThreadFunction(void* v)
{
    while (!kthread_should_stop()) 
    {
        msleep(5000);
    	printk(KERN_DEBUG "FPS sharp : %d\n", fpsCounter);
    	fpsCounter = 0;
    }
    return 0;
}

int thread_fn(void* v) 
{
    //BELOW, 50 becomes 150 becaues we have 3 bits (rgb) per pixel
    int x,y;
    char p0, p1, p01;
    char hasChanged = 0;

    unsigned char *screenBuffer;

    clearDisplay();

    // Below, 204 because we use this structure per line:
    // 1 command byte + 1 addr byte + 200 pixel bytes + 2 padding bytes
    screenBuffer = vzalloc(240*204*sizeof(unsigned char)); 	

    // Init screen to black
    for(y=0 ; y < 240 ; y++)
    {
	gpio_set_value(SCS, 1);
    screenBuffer[y*204] = commandByte;
	screenBuffer[y*204 + 1] = y; //reverseByte(y+1); //sharp display lines are indexed from 1
	screenBuffer[y*204 + 202] = paddingByte;
	screenBuffer[y*204 + 203] = paddingByte;

	//screenBuffer is all to 0 by default (vzalloc)
    spi_write(screen->spi, (const u8 *)(screenBuffer+(y*204)), 204);
	gpio_set_value(SCS, 0);
    }


    while (!kthread_should_stop()) 
    {
        msleep(50);

        for(y=0 ; y < 240 ; y++)
        {
            hasChanged = 0;
        
            // Using 4-bit pixel mode, we can directly copy each line from the framebuffer to the screenBuffer
            // Pixels are 4-bits, so two pixels per byte.  

            for(x=0 ; x<200 ; x++)
            {
                // We work on 2 pixels at a time... 200 * 2 => 400 pixels
                // Each 2 pixels compress indo 1 byte c[] and are copied to the screenBuffer.

                p0 = ioread8((void*)((uintptr_t)info->fix.smem_start + (y*400 + 2*x)));
                p1 = ioread8((void*)((uintptr_t)info->fix.smem_start + (y*400 + 2*x + 1))); 

                // bit shiff p1 by 4 bits and or with p0, so that p0 is the most significant 4 bits
                //p0 = (p0 << 4) + p1; // this worked pretty well with 1 bit colors in 8 bit pixels

                //   red                             green                          blue
                // p0 = (p0 & 0b11100000) > 0 ? 128:0 + (p0 & 0b00011100) > 0 ? 64:0 + (p0 & 0b00000011) > 0 ? 32:0;
                // p1 = (p1 & 0b11100000) > 0 ? 8:0   + (p1 & 0b00011100) > 0 ? 4:0  + (p1 & 0b00000011) > 0 ? 2:0;

                p0 = p0 << 5;
                p1 = p1 << 1;

                p01 = p0 + p1; 

                // check if the pixel has changed
                if (hasChanged)
                {
                    screenBuffer[y*204 + 2 + x] = p01;
                } 
                else if (screenBuffer[y*204 + 2 + x] != p01)
                {
                    hasChanged = 1;
                    screenBuffer[y*204 + 2 + x] = p01;
                }

            }

            if (hasChanged)
            {
                gpio_set_value(SCS, 1);
                spi_write(screen->spi, (const u8 *)(screenBuffer+(y*204)), 204);
                gpio_set_value(SCS, 0);
            }
        }
        //colorCorners(screenBuffer);
        //colorBar(screenBuffer);
    }

    return 0;
}

static int sharp_probe(struct spi_device *spi)
{
    char our_thread[] = "updateScreen";
    char thread_vcom[] = "vcom";
    char thread_fps[] = "fpsThread";
    int retval;

	screen = devm_kzalloc(&spi->dev, sizeof(*screen), GFP_KERNEL);
	if (!screen)
		return -ENOMEM;

	spi->bits_per_word  = 8;
	spi->max_speed_hz   = 2000000;

	screen->spi	= spi;

    spi_set_drvdata(spi, screen);

    thread1 = kthread_create(thread_fn,NULL,our_thread);
    if((thread1))
    {
        wake_up_process(thread1);
    }

    fpsThread = kthread_create(fpsThreadFunction,NULL,thread_fps);
    if((fpsThread))
    {
        wake_up_process(fpsThread);
    }

    vcomToggleThread = kthread_create(vcomToggleFunction,NULL,thread_vcom);
    if((vcomToggleThread))
    {
        wake_up_process(vcomToggleThread);
    }

    gpio_request(SCS, "SCS");
    gpio_direction_output(SCS, 0);

    gpio_request(VCOM, "VCOM");
    gpio_direction_output(VCOM, 0);

    gpio_request(DISP, "DISP");
    gpio_direction_output(DISP, 1);

    // SCREEN PART
    retval = -ENOMEM;

    if (!(videomemory = rvmalloc(videomemorysize)))
        return retval;

    memset(videomemory, 0, videomemorysize);

    info = framebuffer_alloc(sizeof(u32) * 256, &spi->dev);
    if (!info)
        goto err;

    info->screen_base = (char __iomem *)videomemory;
    info->fbops = &vfb_ops;

    info->var = vfb_default;
    vfb_fix.smem_start = (unsigned long) videomemory;
    vfb_fix.smem_len = videomemorysize;
    info->fix = vfb_fix;
    info->par = NULL;
    info->flags = FBINFO_FLAG_DEFAULT;

    retval = fb_alloc_cmap(&info->cmap, 16, 0);
    if (retval < 0)
        goto err1;
    retval = register_framebuffer(info);
    if (retval < 0)
        goto err2;

    fb_info(info, "Virtual frame buffer device, using %ldK of video memory\n",
        videomemorysize >> 10);
    return 0;
err2:
    fb_dealloc_cmap(&info->cmap);
err1:
    framebuffer_release(info);
err:
    rvfree(videomemory, videomemorysize);

    return 0;
}

static void sharp_remove(struct spi_device *spi)
{
        if (info) {
                unregister_framebuffer(info);
                fb_dealloc_cmap(&info->cmap);
                framebuffer_release(info);
        }
	kthread_stop(thread1);
	kthread_stop(fpsThread);
    kthread_stop(vcomToggleThread);
	printk(KERN_CRIT "out of screen module");
	//return 0;
}

static struct spi_driver sharp_driver = {
    .probe          = sharp_probe,
    .remove         = sharp_remove,
	.driver = {
		.name	= "sharp",
		.owner	= THIS_MODULE,
	},
};

module_spi_driver(sharp_driver);

MODULE_AUTHOR("Ael Gain <ael.gain@free.fr>");
MODULE_DESCRIPTION("Sharp memory lcd driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:sharp");
