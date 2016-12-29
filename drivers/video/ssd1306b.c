/*
 * Driver for the Solomon SSD1307 OLED controller
 *
 * Copyright 2012 Free Electrons
 *
 * Licensed under the GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#define SSD1306B_WIDTH		128
#define SSD1306B_HEIGHT		64

#define SSD1306B_DATA			0x40
#define SSD1306B_COMMAND		0x80

#define SSD1306B_CONTRAST		0x81
#define SSD1306B_SEG_REMAP_ON		0xa1
#define SSD1306B_DISPLAY_OFF		0xae
#define SSD1306B_DISPLAY_ON		0xaf
#define SSD1306B_START_PAGE_ADDRESS	0xb0

#define SSD1306B_DC_HIGH "ssd1306b_dc_high"
#define SSD1306B_DC_LOW "ssd1306b_dc_low"
#define SSD1306B_RST_HIGH "ssd1306b_reset_high"
#define SSD1306B_RST_LOW "ssd1306b_reset_low"

struct ssd1306b_par {
	struct spi_device *client;
	struct fb_info *info;
	struct delayed_work		test_work;
	int reset;
	struct pinctrl			*pinctrl;
	struct pinctrl_state		*pins_dc_high;
	struct pinctrl_state		*pins_dc_low;
	struct pinctrl_state		*pins_rst_high;
	struct pinctrl_state		*pins_rst_low;
	 struct regulator 			*ssd1306b_vdd;
};

static struct fb_fix_screeninfo ssd1306b_fix = {
	.id		= "Solomon SSD1306",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.xpanstep	= 1,
	.ypanstep	= 1,
	.ywrapstep	= 1,
	.line_length	= SSD1306B_WIDTH / 8,
	.accel		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo ssd1306b_var = {
	.xres		= SSD1306B_WIDTH,
	.yres		= SSD1306B_HEIGHT,
	.xres_virtual	= SSD1306B_WIDTH,
	.yres_virtual	= SSD1306B_HEIGHT,
	.bits_per_pixel	= 1,
};


static int ssd1306b_write_array(struct spi_device *client, u8 *cmd, u32 len)
{
	int ret = 0;

	ret = spi_write(client, cmd, len );
	if (ret ) {
		printk("Couldn't send spi command, ret %d, len %d\n", ret, len);
		//dev_err(&client->dev, "Couldn't send spi command.\n");
		goto error;
	}

error:

	return ret;
}

static int lcd_inited = 0;
static void ssd1306b_reset(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);
	struct ssd1306b_par *par = info->par;
	int ret;

	if(par->ssd1306b_vdd)
		ret = regulator_enable(par->ssd1306b_vdd);
	msleep(1);

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_rst_high);
		if (ret) {
			printk("select SSD1306B_RST_HIGH failed with %d\n", ret);
		}
		msleep(1);

		ret = pinctrl_select_state(par->pinctrl, par->pins_rst_low);
		if (ret) {
			printk("select SSD1306B_RST_LOW failed with %d\n", ret);
		}

		msleep(1);

		ret = pinctrl_select_state(par->pinctrl, par->pins_rst_high);
		if (ret) {
			printk("select SSD1306B_RST_HIGH failed with %d\n", ret);
		}
	}
}

void ssd1306b_sleep(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);
	struct ssd1306b_par *par = info->par;
	char array[] = {0xAE};
	int ret;

	if(!lcd_inited)
	{
		ssd1306b_reset(client);
	}

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_dc_low);
		if (ret) {
			printk("select pins_dc_low failed with %d\n", ret);
			return;
		}
	}
	ssd1306b_write_array(par->client, array, sizeof(array));

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_rst_low);
		if (ret) {
			printk("select pins_rst_low failed with %d\n", ret);
		}
	}
	lcd_inited = 0;
}

static void ssd1306b_init_lcd(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);
	struct ssd1306b_par *par = info->par;
	int ret;

	//for SSD1306B
	//char array[] = {0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,0xA1,0xC8,0xDA,
	//			0x12,0x81,0xCF,0xD9,0xF1,0xDB,0x30,0xA4,0xA6,0x20,0x00,0xAF};
	//0xAE			display power off
	//0xD5,0x80		Set Display Clock Divide Ratio/Oscillator Frequency
	//0xA8,0x3F		Set MUX ratio to N+1 MUX
	//0xD3,0x00		Set Display Offset
	//0x40			Set Display Start Line
	//0x8D, 0x14		enabling charge pump at 7.5V mode
	//0xA1			Set Segment Re-map
	//0xC8			Set COM Output Scan Direction
	//0xDA, 0x12		Set COM Pins Hardware Configuration
	//0x81,0xCF		Set Contrast Control
	//0xD9,0xF1		Set Pre-charge Period
	//0xDB,0x30		Set VCOMH Deselect Level
	//0xA4			Entire Display ON,	0xA5 can used to test LCD
	//0xA6			Set Normal/Inverse Display
	//0x20,0x00		Set Memory Addressing Mode
	//0xAF			display on

	//for SH1106
	char array[] = {0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0xAD,0x8B,0x32,0xA1,
			0xC8,0xDA,0x12,0x81,0x40,0xD9,0x1f,0xDB,0x40,0xA4,0xA6,0xAF};

	if(lcd_inited)
		return;
	ssd1306b_reset(client);

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_dc_low);
		if (ret) {
			printk("select pins_dc_low failed with %d\n", ret);
			return;
		}
	}

	ssd1306b_write_array(client, array, sizeof(array));

	lcd_inited = 1;
}

#if 0
static void ssd1306b_output_image(struct ssd1306b_par *par,unsigned char * srcbuf)		//output ssd1306b mode directly
{
	int ret;
	//output
	ssd1306b_init_lcd(par->client);

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_dc_high);
		if (ret) {
			printk("select pins_dc_high failed with %d\n", ret);
			return;
		}
	}
	//udelay(2);

	ssd1306b_write_array(par->client, srcbuf, SSD1306B_HEIGHT*SSD1306B_WIDTH/8);
}
#endif

#define SH1106_ADDR_CMD_BASE 0xB0
#define SH1106_PAGE_SIZE 0x8
static void ssd1306b_output_image(struct ssd1306b_par *par,unsigned char * srcbuf)		//for SH1106
{
	int ret;
	int i;
	unsigned char array[] = {SH1106_ADDR_CMD_BASE, 0x0,0x10};		//page address, column address
	int page_bytes;
	unsigned char clear_array[] = {0x0,0x0};		//page address, column address

	//init
	ssd1306b_init_lcd(par->client);

	array[0] = SH1106_ADDR_CMD_BASE;
	page_bytes = SSD1306B_HEIGHT*SSD1306B_WIDTH/64;
	for(i = 0; i<SH1106_PAGE_SIZE; i++)
	{
		if(par->pinctrl)
		{
			ret = pinctrl_select_state(par->pinctrl, par->pins_dc_low);
			if (ret) {
				printk("select pins_dc_low failed with %d\n", ret);
				return;
			}
		}
		ssd1306b_write_array(par->client, array, sizeof(array));
		array[0]++;

		if(par->pinctrl)
		{
			ret = pinctrl_select_state(par->pinctrl, par->pins_dc_high);
			if (ret) {
				printk("select pins_dc_high failed with %d\n", ret);
				return;
			}
		}
		ssd1306b_write_array(par->client, clear_array, sizeof(clear_array));
		ssd1306b_write_array(par->client, srcbuf +  page_bytes *i, page_bytes);
		ssd1306b_write_array(par->client, clear_array, sizeof(clear_array));
	}
}
#if 0
static void ssd1306b_output_image(struct ssd1306b_par *par,unsigned char * srcbuf)		//convert normal mode to ssd1306b mode
{
	int i, j, k,l;
	unsigned char* pbuf;
	int tar_index;
	int ret;

	int height, width, page_size, page_head, index;
	u8 byte, bit;
	u8 tempbuf;

	swapbufferindex = (swapbufferindex + 1) % 2;
	pbuf = ssd1306b_swap_buf + SSD1306B_HEIGHT*SSD1306B_WIDTH*swapbufferindex/8;
	tar_index = 0;

	height = SSD1306B_HEIGHT / 8;
	width = SSD1306B_WIDTH / 8;
	page_size = SSD1306B_WIDTH;
	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			page_head = page_size * i;
			for (l = 7; l >= 0; l--) {			//index in byte
				tempbuf = 0;
				for (k = 0; k < 8; k++) {		//index in each 8 H line
					index = page_head + k * width + j;
					byte = *(srcbuf + index);
					bit = byte & (0x1 << l);
					bit = bit >> l;
					tempbuf |= bit << k;
				}
				*(pbuf + tar_index) = tempbuf;
				tar_index++;
			}
		}
	}
#if 0
	int test_index;
	int test_len;
	test_len = SSD1306B_HEIGHT*SSD1306B_WIDTH/8;
	for(test_index = 0; test_index < test_len; test_index++)
	{
		printk("pbuf[%d], 0x%x\n", test_index, pbuf[test_index]);
	}
#endif

	//output
	ssd1306b_init_lcd(par->client);

	if(par->pinctrl)
	{
		ret = pinctrl_select_state(par->pinctrl, par->pins_dc_high);
		if (ret) {
			printk("select pins_dc_high failed with %d\n", ret);
			return;
		}
	}
	//udelay(2);

	ssd1306b_write_array(par->client, pbuf, SSD1306B_HEIGHT*SSD1306B_WIDTH/8);
}
#endif

//#define ZTE_DUMP_SSD1206B
#ifdef ZTE_DUMP_SSD1206B	//for dump image
#define DUMP_DIR "/cache/test"
static int dump_index = 0;
#define ZTE_BMP_HEAD_SIZE 0x3E		//14 + 40 + 4(black) + 4(white)
#define ZTE_BMP_INFO_SIZE 0x28		//40
unsigned char white_format_file_buff[SSD1306B_WIDTH*SSD1306B_HEIGHT/8 + ZTE_BMP_HEAD_SIZE];
void setint(unsigned char* tar, int data)
{
	int tarIndex = 0;

	tar[tarIndex++] = (unsigned char)(data & 0xff);
	data >>= 8;

	tar[tarIndex++] = (unsigned char)(data & 0xff);
	data >>= 8;

	tar[tarIndex++] = (unsigned char)(data & 0xff);
	data >>= 8;

	tar[tarIndex++] = (unsigned char)(data & 0xff);
}

void setshort(unsigned char* tar, short data)
{
	int tarIndex = 0;

	tar[tarIndex++] = (unsigned char)(data & 0xff);
	data = (short)(data >> 8);

	tar[tarIndex++] = (unsigned char)(data & 0xff);
}
void dump_to_bmpfile(unsigned char * pbuf, int width, int height)		//no bmp header
{
	char dumpFilename[100];
	int index = 0;
	int size = width * height/8;

	mm_segment_t old_fs;
	struct file *file=NULL;

	snprintf(dumpFilename, sizeof(dumpFilename),
		"%s/sfd_kernel_dump%04d_black.bmp", DUMP_DIR,
		dump_index);

	white_format_file_buff[index++] = 'B';
	white_format_file_buff[index++] = 'M';
	setint(white_format_file_buff + index, size + ZTE_BMP_HEAD_SIZE);	//size
	index += 4;
	setint(white_format_file_buff + index, 0);		//reserved
	index += 4;
	setint(white_format_file_buff + index, ZTE_BMP_HEAD_SIZE);		//reserved
	index += 4;
	setint(white_format_file_buff + index, ZTE_BMP_INFO_SIZE);		//info size
	index += 4;
	setint(white_format_file_buff + index, width);		//info size
	index += 4;
	setint(white_format_file_buff + index, height);		//info size
	index += 4;
	setshort(white_format_file_buff + index, 1);		//Planes
	index += 2;
	setshort(white_format_file_buff + index, 1);		//bpp
	index += 2;
	setint(white_format_file_buff + index, 0);		//comp
	index += 4;
	setint(white_format_file_buff + index, size);		//data size
	index += 4;
	setint(white_format_file_buff + index, 0);		//xppm
	index += 4;
	setint(white_format_file_buff + index, 0);		//yppm
	index += 4;
	setint(white_format_file_buff + index, 0);		//cols
	index += 4;
	setint(white_format_file_buff + index, 0);		//Important colours.
	index += 4;
	setint(white_format_file_buff + index, 0);		//black color.
	index += 4;
	setint(white_format_file_buff + index, 0xffffff);		//white color
	index += 4;

	memcpy(white_format_file_buff + index, pbuf, size);

	file=filp_open(dumpFilename,O_CREAT | O_RDWR,0);
	//file=filp_open("/cache/test/kernel_dump.bmp", O_CREAT | O_RDWR,0);
	if(IS_ERR(file)) goto fail0;
	old_fs=get_fs();
	set_fs(get_ds());
	file->f_op->write(file,white_format_file_buff,size + ZTE_BMP_HEAD_SIZE,&file->f_pos);
	set_fs(old_fs);
fail0:
	//filp_close(file,NULL);
	dump_index++;
	if(dump_index > 100)
		dump_index = 0;
}
#endif

#if 0		//ssd1306b format
#define IMAGE_PARTS 64
static void ssd1306b_display_test(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);
	struct ssd1306b_par *par = info->par;
	int index;
	int size = SSD1306B_WIDTH * SSD1306B_HEIGHT/8;
	char image[size];

	for(index = 0;index < IMAGE_PARTS;index++)
	{
		if(index % 8 == 0)
			memset(image + size * index/IMAGE_PARTS,  0xff, size/IMAGE_PARTS);
		else if(index % 8 == 1)
			memset(image + size * index/IMAGE_PARTS,  0x03, size/IMAGE_PARTS);
		else if(index % 8 == 2)
			memset(image + size * index/IMAGE_PARTS,  0x0C, size/IMAGE_PARTS);
		else if(index % 8 == 3)
			memset(image + size * index/IMAGE_PARTS,  0x0F, size/IMAGE_PARTS);
		else if(index % 8 == 4)
			memset(image + size * index/IMAGE_PARTS,  0x30, size/IMAGE_PARTS);
		else if(index % 8 == 5)
			memset(image + size * index/IMAGE_PARTS,  0xC0, size/IMAGE_PARTS);
		else if(index % 8 == 6)
			memset(image + size * index/IMAGE_PARTS,  0xF0, size/IMAGE_PARTS);
		else if(index % 8 == 7)
			memset(image + size * index/IMAGE_PARTS,  0, size/IMAGE_PARTS);
	}

	ssd1306b_output_image(par,image);
}
#endif

//bmp format
#define IMAGE_PARTS 8
static void ssd1306b_display_test(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);
	struct ssd1306b_par *par = info->par;
	int size = SSD1306B_WIDTH * SSD1306B_HEIGHT/8;
	char image[size];
	int index;

	//printk("jiangfeng %s\n", __func__);
	for(index = 0;index < IMAGE_PARTS;index++)
	{
		if(index % 8 == 0)
			memset(image + size * index/IMAGE_PARTS,  0xff, size/IMAGE_PARTS);
		else if(index % 8 == 1)
			memset(image + size * index/IMAGE_PARTS,  0x03, size/IMAGE_PARTS);
		else if(index % 8 == 2)
			memset(image + size * index/IMAGE_PARTS,  0x0C, size/IMAGE_PARTS);
		else if(index % 8 == 3)
			memset(image + size * index/IMAGE_PARTS,  0x0F, size/IMAGE_PARTS);
		else if(index % 8 == 4)
			memset(image + size * index/IMAGE_PARTS,  0x30, size/IMAGE_PARTS);
		else if(index % 8 == 5)
			memset(image + size * index/IMAGE_PARTS,  0xC0, size/IMAGE_PARTS);
		else if(index % 8 == 6)
			memset(image + size * index/IMAGE_PARTS,  0xF0, size/IMAGE_PARTS);
		else if(index % 8 == 7)
			memset(image + size * index/IMAGE_PARTS,  0, size/IMAGE_PARTS);
	}
	ssd1306b_output_image(par,image);
}

static void ssd1306b_update_display(struct ssd1306b_par *par)
{
	u8 *vmem = par->info->screen_base;
#ifdef ZTE_DUMP_SSD1206B	//for dump image
	dump_to_bmpfile(vmem, SSD1306B_WIDTH, SSD1306B_HEIGHT);
#endif
	ssd1306b_output_image(par,vmem);
}

static ssize_t ssd1306b_write(struct fb_info *info, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct ssd1306b_par *par = info->par;
	unsigned long total_size;
	unsigned long p = *ppos;
	u8 __iomem *dst;

	//total_size = info->fix.smem_len;
	total_size = info->fix.smem_len = SSD1306B_WIDTH * SSD1306B_HEIGHT/8;

	if (p > total_size)
		return -EINVAL;

	if (count + p > total_size)
		count = total_size - p;

	if (!count)
		return -EINVAL;

	dst = (void __force *) (info->screen_base + p);

	if (copy_from_user(dst, buf, count))
		return -EFAULT;

	ssd1306b_update_display(par);

	*ppos += count;
	if(*ppos >= total_size)
		*ppos  = 0;

	return count;
}

#if 0
static void ssd1306b_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	//printk("jiangfeng %s\n", __func__);
#if 0
	struct ssd1306b_par *par = info->par;
	sys_fillrect(info, rect);
	ssd1306b_update_display(par);
#endif
}

static void ssd1306b_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	//printk("jiangfeng %s\n", __func__);
#if 0
	struct ssd1306b_par *par = info->par;
	//printk("tangzhengboo %s\n", __func__);
	sys_copyarea(info, area);
	ssd1306b_update_display(par);
#endif
}

static void ssd1306b_imageblit(struct fb_info *info, const struct fb_image *image)
{
	//printk("jiangfeng %s\n", __func__);
#if 0
	struct ssd1306b_par *par = info->par;

	//printk("tangzhengboo %s\n", __func__);
	sys_imageblit(info, image);
	ssd1306b_update_display(par);
#endif
}
#endif

#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
static int ssd1306b_mmap(struct fb_info *info, struct vm_area_struct * vma)
{
	/* Get frame buffer memory range. */
	unsigned long start = info->fix.smem_start;
	u32 len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	//struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;

	if (!start)
		return -EINVAL;

	if ((vma->vm_end <= vma->vm_start) ||
	    (off >= len) ||
	    ((vma->vm_end - vma->vm_start) > (len - off)))
		return -EINVAL;

	/* Set VM flags. */
	start &= PAGE_MASK;
	off += start;
	if (off < start)
		return -EINVAL;

	//printk("jiangfeng %s again, start 0x%x, vm_start 0x%x, end 0x%x, len 0x%x, off 0x%x\n",
	//	__func__, (unsigned int)start, (unsigned int)vma->vm_start, (unsigned int)vma->vm_end, (unsigned int)len, (unsigned int)off);

	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO | VM_RESERVED;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start,
				virt_to_phys((void*)off) >> PAGE_SHIFT, //jiangfeng, shall convert to physical address first
				vma->vm_end - vma->vm_start,
				vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int ssd1306b_check_var(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	//struct ssd1306b_par *par = info->par;
	if (var->rotate != FB_ROTATE_UR)
		return -EINVAL;
	if (var->grayscale != info->var.grayscale)
		return -EINVAL;

	switch (var->bits_per_pixel) {
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if ((var->xres_virtual <= 0) || (var->yres_virtual <= 0))
		return -EINVAL;

	if (info->fix.smem_start) {
		u32 len = var->xres_virtual * var->yres_virtual *
			(var->bits_per_pixel / 8);
		if (len > info->fix.smem_len)
			return -EINVAL;
	}

	if ((var->xres == 0) || (var->yres == 0))
		return -EINVAL;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;

	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	return 0;
}

static int ssd1306b_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	struct ssd1306b_par *par = info->par;
	u8 *vmem = NULL;

	//printk("jiangfeng %s, yoffset %d, yres %d, screen_base 0x%x, start 0x%x, screen_base 0x%x, start 0x%x\n",
	//	__func__, var->yoffset, var->yres, (unsigned int)par->info->screen_base, (unsigned int)par->info->fix.smem_start,
	//	(unsigned int)info->screen_base, (unsigned int)info->fix.smem_start);
	if(var->yoffset == var->yres)
		vmem = par->info->screen_base + SSD1306B_WIDTH * SSD1306B_HEIGHT / 8;
	else if(var->yoffset == 0)
		vmem = par->info->screen_base;
	else
		return -EINVAL;
#ifdef ZTE_DUMP_SSD1206B	//for dump image
	dump_to_bmpfile(vmem, SSD1306B_WIDTH, SSD1306B_HEIGHT);
#endif
	ssd1306b_output_image(par,vmem);
	return 0;
}

unsigned char clear_buf[SSD1306B_HEIGHT*SSD1306B_WIDTH/8];	//two swap buffer
static int first_blank = 1;
static int ssd1306b_blank(int blank_mode, struct fb_info *info)
{
	int ret;
	struct ssd1306b_par *par = info->par;

	if (blank_mode == FB_BLANK_UNBLANK)
	{
		ssd1306b_init_lcd(par->client);
		if(par->pinctrl)
		{
			ret = pinctrl_select_state(par->pinctrl, par->pins_dc_high);
			if (ret) {
				printk("select pins_dc_high failed with %d\n", ret);
				return -EPERM;
			}
		}

		//clear display
		if(first_blank)
		{
			memset(clear_buf, 0, sizeof(clear_buf));
			ssd1306b_output_image(par, clear_buf);
			first_blank = 0;
		}
	}
	else
	{
		ssd1306b_sleep(par->client);
	}
	printk("jiangfeng %s, blank_mode %d\n", __func__, blank_mode);

	return 0;
}

static struct fb_ops ssd1306b_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var = ssd1306b_check_var,	/* vinfo check */
	.fb_read	= fb_sys_read,
	.fb_write	= ssd1306b_write,
	.fb_blank = ssd1306b_blank,	/* blank display */
	.fb_pan_display = ssd1306b_pan_display,	/* pan display */
	//.fb_fillrect	= ssd1306b_fillrect,
	//.fb_copyarea	= ssd1306b_copyarea,
	//.fb_imageblit	= ssd1306b_imageblit,
	.fb_mmap = ssd1306b_mmap,
};

#ifdef CONFIG_FB_DEFERRED_IO
static void ssd1306b_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	//printk("tangzhengboo %s\n", __func__);
	ssd1306b_update_display(info->par);
}

static struct fb_deferred_io ssd1306b_defio = {
	.delay		= HZ,
	.deferred_io	= ssd1306b_deferred_io,
};
#endif

#define SPI_TEST_PERIOD_MS	2000
static void spi_test_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ssd1306b_par *par = container_of(dwork,
				struct ssd1306b_par, test_work);

	ssd1306b_display_test(par->client);
	schedule_delayed_work(&par->test_work, msecs_to_jiffies(SPI_TEST_PERIOD_MS));
}

struct spi_device *clientone;
static int ssd1306b_probe(struct spi_device *client)
{
	struct fb_info *info;
	u32 vmem_size = SSD1306B_WIDTH * SSD1306B_HEIGHT / 8;
	struct ssd1306b_par *par;
	u8 *vmem;
	int ret;
	clientone = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "No device tree data found!\n");
		return -EINVAL;
	}

	info = framebuffer_alloc(sizeof(struct ssd1306b_par), &clientone->dev);
	if (!info) {
		dev_err(&client->dev, "Couldn't allocate framebuffer.\n");
		return -ENOMEM;
	}


	vmem_size = vmem_size > PAGE_SIZE? vmem_size:PAGE_SIZE;
	vmem = kmalloc(vmem_size, GFP_KERNEL);		//not to use devm_kzalloc(), have offset 0x10
	if (!vmem) {
		dev_err(&client->dev, "Couldn't allocate graphical memory.\n");
		ret = -ENOMEM;
		goto probe_error;
	}

	ret = register_framebuffer(info);
#if 0
	if (ret) {
		dev_err(&client->dev, "Couldn't register the framebuffer\n");
		goto probe_error;
	}
#endif

	info->fbops = &ssd1306b_ops;
	info->fix = ssd1306b_fix;
#ifdef CONFIG_FB_DEFERRED_IO
	info->fbdefio = &ssd1306b_defio;
#endif
	info->var = ssd1306b_var;
	info->var.red.length = 1;
	info->var.red.offset = 0;
	info->var.green.length = 1;
	info->var.green.offset = 0;
	info->var.blue.length = 1;
	info->var.blue.offset = 0;

	info->screen_base = (u8 __force __iomem *)vmem;
	info->fix.smem_start = (unsigned long)vmem;
	info->fix.smem_len = vmem_size;

#ifdef CONFIG_FB_DEFERRED_IO
	fb_deferred_io_init(info);
#endif

	par = info->par;
	par->info = info;
	par->client = client;

	dev_info(&client->dev, "fb%d: %s framebuffer device registered, using %d bytes of video memory\n", info->node, info->fix.id, vmem_size);

	par->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(par->pinctrl)) {
		printk("%s, error devm_pinctrl_get(), par->pinctrl 0x%x\n", __func__, (unsigned int)par->pinctrl);
		goto probe_error;
	}
	else
	{
		par->pins_dc_high = pinctrl_lookup_state(par->pinctrl, SSD1306B_DC_HIGH);
		if (IS_ERR_OR_NULL(par->pins_dc_high))
		{
			printk("%s, error pinctrl_lookup_state() for SSD1306B_DC_HIGH\n", __func__);
			goto probe_error;
		}

		par->pins_dc_low = pinctrl_lookup_state(par->pinctrl, SSD1306B_DC_LOW);
		if (IS_ERR_OR_NULL(par->pins_dc_low))
		{
			printk("%s, error pinctrl_lookup_state() for SSD1306B_DC_LOW\n", __func__);
			goto probe_error;
		}

		par->pins_rst_high = pinctrl_lookup_state(par->pinctrl, SSD1306B_RST_HIGH);
		if (IS_ERR_OR_NULL(par->pins_rst_high))
		{
			printk("%s, error pinctrl_lookup_state() for SSD1306B_RST_HIGH\n", __func__);
			goto probe_error;
		}

		par->pins_rst_low = pinctrl_lookup_state(par->pinctrl, SSD1306B_RST_LOW);
		if (IS_ERR_OR_NULL(par->pins_rst_low))
		{
			printk("%s, error pinctrl_lookup_state() for SSD1306B_RST_LOW\n", __func__);
			goto probe_error;
		}
	}

	par->ssd1306b_vdd = devm_regulator_get(&client->dev, "vcp");
	if (IS_ERR(par->ssd1306b_vdd)) {
		printk("unable to get ssd1306b vdd\n");
		ret = PTR_ERR(par->ssd1306b_vdd);
		par->ssd1306b_vdd = NULL;
		goto probe_error;
	}

	INIT_DELAYED_WORK(&par->test_work, spi_test_work);
	//schedule_delayed_work(&par->test_work, msecs_to_jiffies(20000));		//for test
	spi_set_drvdata(client, info);

	return 0;

probe_error:
	unregister_framebuffer(info);
#ifdef CONFIG_FB_DEFERRED_IO
	fb_deferred_io_cleanup(info);
#endif
	framebuffer_release(info);
	return ret;
}

static int ssd1306b_remove(struct spi_device *client)
{
	struct fb_info *info = spi_get_drvdata(client);

	unregister_framebuffer(info);
#ifdef CONFIG_FB_DEFERRED_IO
	fb_deferred_io_cleanup(info);
#endif
	framebuffer_release(info);

	return 0;
}

static const struct spi_device_id ssd1306b_spi_id[] = {
	{ "ssd1306b", 0 },
	{ }
};

#if 0
#ifdef CONFIG_PM_SLEEP
static int ssd1306b_pm_suspend(struct device *client)
{
	return 0;
}

static int ssd1306b_pm_resume(struct device *client)
{
	struct fb_info *info = spi_get_drvdata((struct spi_device*)client);
	if (!info)
		return -ENODEV;
	ssd1306b_blank(FB_BLANK_UNBLANK, info);
	return 0;
}
#endif

static const struct dev_pm_ops ssd1306b_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ssd1306b_pm_suspend, ssd1306b_pm_resume)
};
#endif

MODULE_DEVICE_TABLE(spi, ssd1306b_spi_id);

static const struct of_device_id ssd1306b_of_match[] = {
	{ .compatible = "solomon,ssd1306b-spi" },
	{},
};
MODULE_DEVICE_TABLE(of, ssd1306b_of_match);

static struct spi_driver ssd1306b_spi_driver = {
	.probe = ssd1306b_probe,
	.remove = ssd1306b_remove,
	.id_table = ssd1306b_spi_id,
	.driver = {
		.name = "ssd1306b",
		.of_match_table = of_match_ptr(ssd1306b_of_match),
		.owner = THIS_MODULE,
		//.pm = &ssd1306b_pm_ops,
	},
};

static int __init ssd1306b_spi_init(void)
{
	return spi_register_driver(&ssd1306b_spi_driver);
}

static void __exit ssd1306b_spi_exit(void)
{
	spi_unregister_driver(&ssd1306b_spi_driver);
}

module_init(ssd1306b_spi_init);
module_exit(ssd1306b_spi_exit);

MODULE_DESCRIPTION("FB driver for the Solomon SSD1306 OLED controller");
MODULE_AUTHOR("Tang Zhengbo <tang.zhengbo@zte.com.cn>");
MODULE_LICENSE("GPL");

