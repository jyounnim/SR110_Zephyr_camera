/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/
/*
 * Lab C4 (TFT still preview) tuning: this decoder is vendored
 * verbatim from ChaN's TJpgDec (see tjpgd.c header for license and
 * version info) except for this configuration file, adjusted so its
 * output format matches what the ST7789V3 panel wants directly
 * (RGB565) instead of the default RGB888 - see tft_still.c.
 */

#define	JD_SZBUF		512
/* Specifies size of stream input buffer */

#define JD_FORMAT		1
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)  <- selected: matches the ST7789V3's native format
/  2: Grayscale (8-bit/pix)
*/

#define	JD_USE_SCALE	1
/* Switches output descaling feature.
/  0: Disable
/  1: Enable  <- selected: lets us decode straight to a half-size
/     (240x135) image instead of the full 480x270, halving both the
/     framebuffer size and the decode work.
*/

#define JD_TBLCLIP		1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/

#define JD_FASTDECODE	0
/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.  <- selected: smallest code size
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
*/
