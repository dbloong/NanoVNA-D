/*
 * Copyright (c) 2019-2020, Dmitry (DiSlord) dislordlive@gmail.com
 * Based on TAKAHASHI Tomohiro (TTRFTECH) edy555@gmail.com
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include "ch.h"

// Need enable HAL_USE_SPI in halconf.h
#define __USE_DISPLAY_DMA__
// LCD or hardware allow change brightness, add menu item for this
//#define __LCD_BRIGHTNESS__
// Use DAC (in H4 used for brightness used DAC, so need enable __LCD_BRIGHTNESS__ for it)
//#define __VNA_ENABLE_DAC__
// Allow enter to DFU from menu or command
#define __DFU_SOFTWARE_MODE__
// Add RTC clock support
#define __USE_RTC__
// Add SD card support, req enable RTC (additional settings for file system see FatFS lib ffconf.h)
#define __USE_SD_CARD__
// If enabled serial in halconf.h, possible enable serial console control
#define __USE_SERIAL_CONSOLE__
// Add LC match function
#define __USE_LC_MATCHING__
// Use buildin table for sin/cos calculation, allow save a lot of flash space (this table also use for FFT), max sin/cos error = 4e-7
#define __VNA_USE_MATH_TABLES__

/*
 * main.c
 */

// Minimum frequency set
#define START_MIN                800
// Maximum frequency set
#define STOP_MAX                 2700000000U
// Frequency threshold (max frequency for si5351, harmonic mode after)
#define FREQUENCY_THRESHOLD      300000100U

// Define ADC sample rate in kilobyte (can be 48k, 96k, 192k, 384k)
//#define AUDIO_ADC_FREQ_K        768
//#define AUDIO_ADC_FREQ_K        384
#define AUDIO_ADC_FREQ_K        192
//#define AUDIO_ADC_FREQ_K        96
//#define AUDIO_ADC_FREQ_K        48

// Define sample count for one step measure
#define AUDIO_SAMPLES_COUNT   (48)
//#define AUDIO_SAMPLES_COUNT   (96)
//#define AUDIO_SAMPLES_COUNT   (192)

// Frequency offset, depend from AUDIO_ADC_FREQ settings (need aligned table)
// Use real time build table (undef for use constant, see comments)
// Constant tables build only for AUDIO_SAMPLES_COUNT = 48
//#define USE_VARIABLE_OFFSET

#if AUDIO_ADC_FREQ_K == 768
// For 768k ADC    (16k step for 48 samples)
#define FREQUENCY_IF_K         12
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         32
//#define FREQUENCY_IF_K         48
//#define FREQUENCY_IF_K         64

#elif AUDIO_ADC_FREQ_K == 384
// For 384k ADC    (8k step for 48 samples)
//#define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K         12  // only 96 samples and variable table
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         20  // only 96 samples and variable table
//#define FREQUENCY_IF_K         24
//#define FREQUENCY_IF_K         32

#elif AUDIO_ADC_FREQ_K == 192
// For 192k ADC (sin_cos table in dsp.c generated for 8k, 12k, 16k, 20k, 24k if change need create new table )
//#define FREQUENCY_IF_K          8
#define FREQUENCY_IF_K         12
//#define FREQUENCY_IF_K         16
//#define FREQUENCY_IF_K         20
//#define FREQUENCY_IF_K         24
//#define FREQUENCY_IF_K         28

#elif AUDIO_ADC_FREQ_K == 96
// For 96k ADC (sin_cos table in dsp.c generated for 6k, 8k, 10k, 12k if change need create new table )
//#define FREQUENCY_IF_K          6
//#define FREQUENCY_IF_K          8
//#define FREQUENCY_IF_K         10
#define FREQUENCY_IF_K         12

#elif AUDIO_ADC_FREQ_K == 48
// For 48k ADC (sin_cos table in dsp.c generated for 3k, 4k, 5k, 6k, if change need create new table )
//#define FREQUENCY_IF_K          3
//#define FREQUENCY_IF_K          4
//#define FREQUENCY_IF_K          5
#define FREQUENCY_IF_K          6
//#define FREQUENCY_IF_K          7
#endif

#define AUDIO_ADC_FREQ       (AUDIO_ADC_FREQ_K*1000)
#define FREQUENCY_OFFSET     (FREQUENCY_IF_K*1000)

// Apply calibration after made sweep, (if set 1, then calibration move out from sweep cycle)
#define APPLY_CALIBRATION_AFTER_SWEEP 0

// Speed of light const
#define SPEED_OF_LIGHT           299792458

// pi const
#define VNA_PI                   3.14159265358979323846

// Maximum sweep point count (limit by flash and RAM size)
#define POINTS_COUNT             101

// Optional sweep point (in UI menu)
#if POINTS_COUNT >=401
#define POINTS_SET_COUNT       5
#define POINTS_SET             {51, 101, 201, 301, POINTS_COUNT}
#define POINTS_COUNT_DEFAULT   POINTS_COUNT
#elif POINTS_COUNT >=301
#define POINTS_SET_COUNT       4
#define POINTS_SET             {51, 101, 201, POINTS_COUNT}
#define POINTS_COUNT_DEFAULT   POINTS_COUNT
#elif POINTS_COUNT >=201
#define POINTS_SET_COUNT       3
#define POINTS_SET             {51, 101, POINTS_COUNT}
#define POINTS_COUNT_DEFAULT   POINTS_COUNT
#elif POINTS_COUNT >=101
#define POINTS_SET_COUNT       2
#define POINTS_SET             {51, POINTS_COUNT}
#define POINTS_COUNT_DEFAULT   POINTS_COUNT
#endif

extern float measured[2][POINTS_COUNT][2];
extern uint32_t frequencies[POINTS_COUNT];

#define CAL_LOAD  0
#define CAL_OPEN  1
#define CAL_SHORT 2
#define CAL_THRU  3
#define CAL_ISOLN 4

#define CALSTAT_LOAD (1<<0)
#define CALSTAT_OPEN (1<<1)
#define CALSTAT_SHORT (1<<2)
#define CALSTAT_THRU (1<<3)
#define CALSTAT_ISOLN (1<<4)
#define CALSTAT_ES (1<<5)
#define CALSTAT_ER (1<<6)
#define CALSTAT_ET (1<<7)
#define CALSTAT_ED CALSTAT_LOAD
#define CALSTAT_EX CALSTAT_ISOLN
#define CALSTAT_APPLY (1<<8)
#define CALSTAT_INTERPOLATED (1<<9)

#define ETERM_ED 0 /* error term directivity */
#define ETERM_ES 1 /* error term source match */
#define ETERM_ER 2 /* error term refrection tracking */
#define ETERM_ET 3 /* error term transmission tracking */
#define ETERM_EX 4 /* error term isolation */

#define DOMAIN_MODE (1<<0)
#define DOMAIN_FREQ (0<<0)
#define DOMAIN_TIME (1<<0)
#define TD_FUNC (0b11<<1)
#define TD_FUNC_BANDPASS (0b00<<1)
#define TD_FUNC_LOWPASS_IMPULSE (0b01<<1)
#define TD_FUNC_LOWPASS_STEP    (0b10<<1)
#define TD_WINDOW (0b11<<3)
#define TD_WINDOW_NORMAL (0b00<<3)
#define TD_WINDOW_MINIMUM (0b01<<3)
#define TD_WINDOW_MAXIMUM (0b10<<3)
// L/C match enable option
#define TD_LC_MATH        (1<<5)

#if   POINTS_COUNT <= 256
#define FFT_SIZE   256
#elif POINTS_COUNT <= 512
#define FFT_SIZE   512
#endif

// Return sin/cos value, angle have range 0.0 to 1.0 (0 is 0 degree, 1 is 360 degree)
void vna_sin_cos(float angle, float * pSinVal, float * pCosVal);

void cal_collect(uint16_t type);
void cal_done(void);

#define MAX_FREQ_TYPE 5
enum stimulus_type {
  ST_START=0, ST_STOP, ST_CENTER, ST_SPAN, ST_CW
};

void set_marker_index(int m, int idx);
void set_sweep_frequency(int type, uint32_t frequency);
uint32_t get_sweep_frequency(int type);
void set_bandwidth(uint16_t bw_count);
uint32_t get_bandwidth_frequency(uint16_t bw_freq);
void set_power(uint8_t value);

int32_t  my_atoi(const char *p);
uint32_t my_atoui(const char *p);
double   my_atof(const char *p);

void toggle_sweep(void);
void load_default_properties(void);
int  load_properties(uint32_t id);
void set_sweep_points(uint16_t points);

#define SWEEP_ENABLE  0x01
#define SWEEP_ONCE    0x02
#define SWEEP_BINARY  0x08

extern  uint8_t sweep_mode;
extern const char *info_about[];

/*
 * dsp.c
 */
// Define aic3204 source clock frequency (on 8MHz used fractional multiplier, and possible little phase error)
#define AUDIO_CLOCK_REF       (8000000U)
// Define aic3204 source clock frequency (on 12288000U used integer multiplier)
//#define AUDIO_CLOCK_REF       (12288000U)
// Define aic3204 source clock frequency (on 10752000U used integer multiplier)
//#define AUDIO_CLOCK_REF       (10752000U)
// Disable AIC PLL clock, use input as CODEC_CLKIN (not stable on some devices, on long work)
//#define AUDIO_CLOCK_REF       (98304000U)

// Buffer contain left and right channel samples (need x2)
#define AUDIO_BUFFER_LEN      (AUDIO_SAMPLES_COUNT*2)

// Bandwidth depend from AUDIO_SAMPLES_COUNT and audio ADC frequency
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  48kHz one measure give  48000/48=1000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC =  96kHz one measure give  96000/48=2000Hz
// for AUDIO_SAMPLES_COUNT = 48 and ADC = 192kHz one measure give 192000/48=4000Hz
// Define additional measure count for menus
#if AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 16000
#define BANDWIDTH_8000            (  1 - 1)
#define BANDWIDTH_4000            (  2 - 1)
#define BANDWIDTH_1000            (  8 - 1)
#define BANDWIDTH_333             ( 24 - 1)
#define BANDWIDTH_100             ( 80 - 1)
#define BANDWIDTH_30              (256 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 8000
#define BANDWIDTH_8000            (  1 - 1)
#define BANDWIDTH_4000            (  2 - 1)
#define BANDWIDTH_1000            (  8 - 1)
#define BANDWIDTH_333             ( 24 - 1)
#define BANDWIDTH_100             ( 80 - 1)
#define BANDWIDTH_30              (256 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 4000
#define BANDWIDTH_4000            (  1 - 1)
#define BANDWIDTH_2000            (  2 - 1)
#define BANDWIDTH_1000            (  4 - 1)
#define BANDWIDTH_333             ( 12 - 1)
#define BANDWIDTH_100             ( 40 - 1)
#define BANDWIDTH_30              (132 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 2000
#define BANDWIDTH_2000            (  1 - 1)
#define BANDWIDTH_1000            (  2 - 1)
#define BANDWIDTH_333             (  6 - 1)
#define BANDWIDTH_100             ( 20 - 1)
#define BANDWIDTH_30              ( 66 - 1)
#define BANDWIDTH_10              (200 - 1)
#elif AUDIO_ADC_FREQ/AUDIO_SAMPLES_COUNT == 1000
#define BANDWIDTH_1000            (  1 - 1)
#define BANDWIDTH_333             (  3 - 1)
#define BANDWIDTH_100             ( 10 - 1)
#define BANDWIDTH_30              ( 33 - 1)
#define BANDWIDTH_10              (100 - 1)
#endif

#ifdef ENABLED_DUMP
extern int16_t ref_buf[];
extern int16_t samp_buf[];
#endif

void dsp_process(int16_t *src, size_t len);
void reset_dsp_accumerator(void);
void calculate_gamma(float *gamma);
void fetch_amplitude(float *gamma);
void fetch_amplitude_ref(float *gamma);
void generate_DSP_Table(int offset);

/*
 * tlv320aic3204.c
 */

void tlv320aic3204_init(void);
void tlv320aic3204_set_gain(uint8_t lgain, uint8_t rgain);
void tlv320aic3204_select(uint8_t channel);
void tlv320aic3204_write_reg(uint8_t page, uint8_t reg, uint8_t data);

/*
 * plot.c
 */
// LCD display size settings
#define LCD_WIDTH                   320
#define LCD_HEIGHT                  240

// Define maximum distance in pixel for pickup marker (can be bigger for big displays)
#define MARKER_PICKUP_DISTANCE    20
// Used marker size settings
#define _USE_BIG_MARKER_     0
// Used font settings
#define _USE_FONT_           0

#if _USE_FONT_ == 0
extern const uint8_t x5x7_bits[];
#define FONT_START_CHAR   0x17
#define FONT_MAX_WIDTH       7
#define FONT_WIDTH           5
#define FONT_GET_HEIGHT      7
#define FONT_STR_HEIGHT      8
#define FONT_GET_DATA(ch)    (  &x5x7_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)   (8-(x5x7_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT]&7))

#elif _USE_FONT_ == 1
extern const uint8_t x7x11b_bits[];
#define FONT_START_CHAR   0x17
#define FONT_MAX_WIDTH       8
#define FONT_WIDTH           7
#define FONT_GET_HEIGHT     11
#define FONT_STR_HEIGHT     11
#define FONT_GET_DATA(ch)   (  &x7x11b_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT])
#define FONT_GET_WIDTH(ch)  (8-(x7x11b_bits[(ch-FONT_START_CHAR)*FONT_GET_HEIGHT]&7))

#elif _USE_FONT_ == 2
extern const uint8_t x10x14_bits[];
#define FONT_START_CHAR   0x17
#define FONT_MAX_WIDTH      14
#define FONT_WIDTH          11
#define FONT_GET_HEIGHT     14
#define FONT_STR_HEIGHT     16
#define FONT_GET_DATA(ch)   (   &x10x14_bits[(ch-FONT_START_CHAR)*2*FONT_GET_HEIGHT  ])
#define FONT_GET_WIDTH(ch)  (14-(x10x14_bits[(ch-FONT_START_CHAR)*2*FONT_GET_HEIGHT+1]&0x7))
#endif

extern const uint8_t numfont16x22[];
#define NUM_FONT_GET_WIDTH      16
#define NUM_FONT_GET_HEIGHT     22
#define NUM_FONT_GET_DATA(ch)   (&numfont16x22[ch*2*NUM_FONT_GET_HEIGHT])

// Offset of plot area (size of additional info at left side)
#define OFFSETX 10
#define OFFSETY  0

// WIDTH better be n*(POINTS_COUNT-1)
#define WIDTH  300
// HEIGHT = 8*GRIDY
#define HEIGHT 232

//#define NGRIDY 10
#define NGRIDY 8

#define FREQUENCIES_XPOS1 OFFSETX
#define FREQUENCIES_XPOS2 206
#define FREQUENCIES_XPOS3 135
#define FREQUENCIES_YPOS  (LCD_HEIGHT-FONT_GET_HEIGHT)

// GRIDX calculated depends from frequency span
//#define GRIDY 29
#define GRIDY (HEIGHT / NGRIDY)

// Need for reference marker draw
#define CELLOFFSETX 5
#define AREA_WIDTH_NORMAL  (CELLOFFSETX + WIDTH  + 1 + 4)
#define AREA_HEIGHT_NORMAL (              HEIGHT + 1)

// Smith/polar chart
#define P_CENTER_X (CELLOFFSETX + WIDTH/2)
#define P_CENTER_Y (HEIGHT/2)
#define P_RADIUS   (HEIGHT/2)

extern int16_t area_width;
extern int16_t area_height;

// Maximum menu buttons count
#define MENU_BUTTON_MAX         8
// Menu buttons size
#define MENU_BUTTON_WIDTH      66
#define MENU_BUTTON_HEIGHT     29
#define MENU_BUTTON_BORDER      1
#define KEYBOARD_BUTTON_BORDER  2

// Define message box width
#define MESSAGE_BOX_WIDTH     180

// Height of numerical input field (at bottom)
#define NUM_INPUT_HEIGHT   32

// On screen keyboard button size
// Use full screen keyboard
#if 1
#define KP_WIDTH                  ((LCD_WIDTH) / 4)                     // numeric keypad button width
#define KP_HEIGHT                 ((LCD_HEIGHT - NUM_INPUT_HEIGHT) / 4) // numeric keypad button height
// Key x, y position (0 - 15) on screen
#define KP_GET_X(posx)            ((posx) * KP_WIDTH)                   // numeric keypad left
#define KP_GET_Y(posy)            ((posy) * KP_HEIGHT)                  // numeric keypad top
#else
// Use less size keyboard
#define KP_WIDTH     48
#define KP_HEIGHT    48
// Key x, y position (0 - 15) on screen
#define KP_GET_X(posx) ((posx)*KP_WIDTH + (LCD_WIDTH-64-KP_WIDTH*4))
#define KP_GET_Y(posy) ((posy)*KP_HEIGHT + 12 )
#endif

#ifdef __USE_LC_MATCHING__
// X and Y offset to L/C match text
 #define STR_LC_MATH_X      (OFFSETX +  0)
// Better be aligned by cell
 #define STR_LC_MATH_Y      (OFFSETY + 32)
// 1/3 Width of text (need 3 column for data)
 #define STR_LC_MATH_WIDTH  (FONT_WIDTH * 10)
// String Height (need 2 + 0..4 string)
 #define STR_LC_MATH_HEIGHT (FONT_STR_HEIGHT + 2)
#endif

// Additional chars in fonts
#define S_DELTA    "\027"  // hex 0x17
#define S_SARROW   "\030"  // hex 0x18
#define S_INFINITY "\031"  // hex 0x19
#define S_LARROW   "\032"  // hex 0x1A
#define S_RARROW   "\033"  // hex 0x1B
#define S_PI       "\034"  // hex 0x1C
#define S_MICRO    "\035"  // hex 0x1D
#define S_OHM      "\036"  // hex 0x1E
#define S_DEGREE   "\037"  // hex 0x1F

// Max palette indexes in config
#define MAX_PALETTE     24

// trace 
#define MAX_TRACE_TYPE 13
enum trace_type {
  TRC_LOGMAG=0, TRC_PHASE, TRC_DELAY, TRC_SMITH, TRC_POLAR, TRC_LINEAR, TRC_SWR, TRC_REAL, TRC_IMAG, TRC_R, TRC_X, TRC_Q, TRC_OFF
};
// Mask for define rectangular plot
#define RECTANGULAR_GRID_MASK ((1<<TRC_LOGMAG)|(1<<TRC_PHASE)|(1<<TRC_DELAY)|(1<<TRC_LINEAR)|(1<<TRC_SWR)|(1<<TRC_REAL)|(1<<TRC_IMAG)|(1<<TRC_R)|(1<<TRC_X)|(1<<TRC_Q))

// LOGMAG: SCALE, REFPOS, REFVAL
// PHASE: SCALE, REFPOS, REFVAL
// DELAY: SCALE, REFPOS, REFVAL
// SMITH: SCALE, <REFPOS>, <REFVAL>
// LINMAG: SCALE, REFPOS, REFVAL
// SWR: SCALE, REFPOS, REFVAL
// Electrical Delay
// Phase

// marker smith value format
enum marker_smithvalue {
  MS_LIN, MS_LOG, MS_REIM, MS_RX, MS_RLC
};

// config._mode flags
#define VNA_MODE_START_STOP       0x00
#define VNA_MODE_CENTER_SPAN      0x01
#define VNA_MODE_DOTTED_GRID      0x02
// Connection flag
#define VNA_MODE_CONNECTION_MASK  0x04
#define VNA_MODE_SERIAL           0x04
#define VNA_MODE_USB              0x00

#define TRACES_MAX 4
typedef struct trace {
  uint8_t enabled;
  uint8_t type;
  uint8_t channel;
  uint8_t reserved;
  float scale;
  float refpos;
} trace_t;

// marker
#define MARKERS_MAX 4
typedef struct marker {
  uint8_t  enabled;
  uint8_t  reserved;
  uint16_t index;
  uint32_t frequency;
} marker_t;

typedef struct config {
  uint32_t magic;
  uint32_t harmonic_freq_threshold;
  uint16_t dac_value;
  int16_t  touch_cal[4];
  uint16_t vbat_offset;
  uint16_t bandwidth;
  uint16_t lcd_palette[MAX_PALETTE];
  uint32_t _serial_speed;
  uint32_t _serial_config;
  uint8_t  _mode;
  uint8_t _brightness;
  uint8_t _reserved[24];
  uint32_t checksum;
} config_t; // sizeof = 108

typedef struct properties {
  uint32_t magic;
  uint32_t _frequency0;
  uint32_t _frequency1;
  uint16_t _sweep_points;
  uint16_t _cal_status;

  float _cal_data[5][POINTS_COUNT][2];
  float _electrical_delay; // picoseconds

  trace_t _trace[TRACES_MAX];
  marker_t _markers[MARKERS_MAX];

  float _velocity_factor; // %
  int8_t _active_marker;
  uint8_t _domain_mode; /* 0bxxxxxffm : where ff: TD_FUNC m: DOMAIN_MODE */
  uint8_t _marker_smith_format;
  uint8_t _power;
  uint32_t checksum;
} properties_t;
//on POINTS_COUNT = 101, sizeof(properties_t) == 4152 (need reduce size on 56 bytes to 4096 for more compact save slot size)

extern config_t config;
extern properties_t *active_props;
extern properties_t current_props;

void set_trace_type(int t, int type);
void set_trace_channel(int t, int channel);
void set_trace_scale(int t, float scale);
void set_trace_refpos(int t, float refpos);
float get_trace_scale(int t);
float get_trace_refpos(int t);
const char *get_trace_typename(int t);

//
// Shell config functions and macros for Serial connect, not used if Serial mode disabled
void shell_update_speed(void);
void shell_reset_console(void);

void set_electrical_delay(float picoseconds);
float get_electrical_delay(void);
float groupdelay_from_array(int i, float array[POINTS_COUNT][2]);

void plot_init(void);
void update_grid(void);
void request_to_redraw_grid(void);
void redraw_frame(void);
//void redraw_all(void);
void request_to_draw_cells_behind_menu(void);
void request_to_draw_cells_behind_numeric_input(void);
void redraw_marker(int8_t marker);
void plot_into_index(float measured[2][POINTS_COUNT][2]);
void force_set_markmap(void);
void draw_frequencies(void);
void draw_all(bool flush);

void draw_cal_status(void);

//void markmap_all_markers(void);

int distance_to_index(int8_t t, uint16_t idx, int16_t x, int16_t y);
int search_nearest_index(int x, int y, int t);

// Marker search functions
#define MK_SEARCH_LEFT    -1
#define MK_SEARCH_RIGHT    1
#define MK_SEARCH_MIN      0
#define MK_SEARCH_MAX      1
void set_marker_search(int16_t mode);
int marker_search(void);
int marker_search_dir(int16_t from, int16_t dir);

// _request flag for update screen
#define REDRAW_CELLS      (1<<0)
#define REDRAW_FREQUENCY  (1<<1)
#define REDRAW_CAL_STATUS (1<<2)
#define REDRAW_MARKER     (1<<3)
#define REDRAW_BATTERY    (1<<4)
#define REDRAW_AREA       (1<<5)
extern  uint8_t redraw_request;

/*
 * ili9341.c
 */
// Set display buffers count for cell render (if use 2 and DMA, possible send data and prepare new in some time)
#ifdef __USE_DISPLAY_DMA__
// Cell size = sizeof(spi_buffer), but need wait while cell cata send to LCD
#define DISPLAY_CELL_BUFFER_COUNT     1
// Cell size = sizeof(spi_buffer)/2, while one cell send to LCD by DMA, CPU render to next cell
//#define DISPLAY_CELL_BUFFER_COUNT     2
#else
// Always one if no DMA mode
#define DISPLAY_CELL_BUFFER_COUNT     1
#endif

// Default LCD brightness if display support it
#define DEFAULT_BRIGHTNESS  70

// Define LCD pixel format
//#define LCD_8BIT_MODE
#define LCD_16BIT_MODE

#ifdef LCD_8BIT_MODE
typedef uint8_t pixel_t;
//  8-bit RRRGGGBB
//#define RGB332(r,g,b)  ( (((r)&0xE0)>>0) | (((g)&0xE0)>>3) | (((b)&0xC0)>>5))
#define RGB565(r,g,b)  ( (((r)&0xE0)>>0) | (((g)&0xE0)>>3) | (((b)&0xC0)>>6))
#define RGBHEX(hex)    ( (((hex)&0xE00000)>>16) | (((hex)&0x00E000)>>11) | (((hex)&0x0000C0)>>6) )
#define HEXRGB(hex)    ( (((hex)<<16)&0xE00000) | (((hex)<<11)&0x00E000) | (((hex)<<6)&0x0000C0) )
#define LCD_PIXEL_SIZE        1
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH  (64/DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (64)
// Define size of screen buffer in pixel_t
#define SPI_BUFFER_SIZE             4096
#endif

#ifdef LCD_16BIT_MODE
typedef uint16_t pixel_t;
// SPI bus revert byte order
// 16-bit gggBBBbb RRRrrGGG
#define RGB565(r,g,b)  ( (((g)&0x1c)<<11) | (((b)&0xf8)<<5) | ((r)&0xf8) | (((g)&0xe0)>>5) )
#define RGBHEX(hex) ( (((hex)&0x001c00)<<3) | (((hex)&0x0000f8)<<5) | (((hex)&0xf80000)>>16) | (((hex)&0x00e000)>>13) )
#define HEXRGB(hex) ( (((hex)>>3)&0x001c00) | (((hex)>>5)&0x0000f8) | (((hex)<<16)&0xf80000) | (((hex)<<13)&0x00e000) )
#define LCD_PIXEL_SIZE        2
// Cell size, depends from spi_buffer size, CELLWIDTH*CELLHEIGHT*sizeof(pixel) <= sizeof(spi_buffer)
#define CELLWIDTH  (64/DISPLAY_CELL_BUFFER_COUNT)
#define CELLHEIGHT (32)
// Define size of screen buffer in pixel_t
#define SPI_BUFFER_SIZE             2048
#endif

#ifndef SPI_BUFFER_SIZE
#error "Define LCD pixel format"
#endif

#define LCD_BG_COLOR             0
#define LCD_FG_COLOR             1
#define LCD_GRID_COLOR           2
#define LCD_MENU_COLOR           3
#define LCD_MENU_TEXT_COLOR      4
#define LCD_MENU_ACTIVE_COLOR    5
#define LCD_TRACE_1_COLOR        6
#define LCD_TRACE_2_COLOR        7
#define LCD_TRACE_3_COLOR        8
#define LCD_TRACE_4_COLOR        9
#define LCD_NORMAL_BAT_COLOR    10
#define LCD_LOW_BAT_COLOR       11
#define LCD_SPEC_INPUT_COLOR    12
#define LCD_RISE_EDGE_COLOR     13
#define LCD_FALLEN_EDGE_COLOR   14
#define LCD_SWEEP_LINE_COLOR    15
#define LCD_BW_TEXT_COLOR       16
#define LCD_INPUT_TEXT_COLOR    17
#define LCD_INPUT_BG_COLOR      18
#define LCD_LC_MATCH_COLOR      19

#define LCD_DEFAULT_PALETTE {\
[LCD_BG_COLOR         ] = RGB565(  0,  0,  0), \
[LCD_FG_COLOR         ] = RGB565(255,255,255), \
[LCD_GRID_COLOR       ] = RGB565(128,128,128), \
[LCD_MENU_COLOR       ] = RGB565(230,230,230), \
[LCD_MENU_TEXT_COLOR  ] = RGB565(  0,  0,  0), \
[LCD_MENU_ACTIVE_COLOR] = RGB565(210,210,210), \
[LCD_TRACE_1_COLOR    ] = RGB565(255,255,  0), \
[LCD_TRACE_2_COLOR    ] = RGB565(  0,255,255), \
[LCD_TRACE_3_COLOR    ] = RGB565(  0,255,  0), \
[LCD_TRACE_4_COLOR    ] = RGB565(255,  0,255), \
[LCD_NORMAL_BAT_COLOR ] = RGB565( 31,227,  0), \
[LCD_LOW_BAT_COLOR    ] = RGB565(255,  0,  0), \
[LCD_SPEC_INPUT_COLOR ] = RGB565(128,255,128), \
[LCD_RISE_EDGE_COLOR  ] = RGB565(255,255,255), \
[LCD_FALLEN_EDGE_COLOR] = RGB565(128,128,128), \
[LCD_SWEEP_LINE_COLOR ] = RGB565(  0,  0,255), \
[LCD_BW_TEXT_COLOR    ] = RGB565(128,128,128), \
[LCD_INPUT_TEXT_COLOR ] = RGB565(  0,  0,  0), \
[LCD_INPUT_BG_COLOR   ] = RGB565(255,255,255), \
[LCD_LC_MATCH_COLOR   ] = RGB565(255,255,255), \
}

#define GET_PALTETTE_COLOR(idx)  config.lcd_palette[idx]

extern pixel_t foreground_color;
extern pixel_t background_color;

extern pixel_t spi_buffer[SPI_BUFFER_SIZE];

// Used for easy define big Bitmap as 0bXXXXXXXXX image
#define _BMP8(d)                                                        ((d)&0xFF)
#define _BMP16(d)                                      (((d)>>8)&0xFF), ((d)&0xFF)
#define _BMP24(d)                    (((d)>>16)&0xFF), (((d)>>8)&0xFF), ((d)&0xFF)
#define _BMP32(d)  (((d)>>24)&0xFF), (((d)>>16)&0xFF), (((d)>>8)&0xFF), ((d)&0xFF)

void ili9341_init(void);
void ili9341_test(int mode);
void ili9341_bulk(int x, int y, int w, int h);
void ili9341_bulk_continue(int x, int y, int w, int h);
void ili9341_bulk_finish(void);
void ili9341_fill(int x, int y, int w, int h);
pixel_t *ili9341_get_cell_buffer(void);

void ili9341_set_foreground(uint16_t fg_idx);
void ili9341_set_background(uint16_t bg_idx);
void ili9341_clear_screen(void);
void ili9341_blitBitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *bitmap);
void ili9341_drawchar(uint8_t ch, int x, int y);
void ili9341_drawstring(const char *str, int x, int y);
void ili9341_drawstringV(const char *str, int x, int y);
int  ili9341_drawchar_size(uint8_t ch, int x, int y, uint8_t size);
void ili9341_drawstring_size(const char *str, int x, int y, uint8_t size);
void ili9341_drawfont(uint8_t ch, int x, int y);
void ili9341_read_memory(int x, int y, int w, int h, uint16_t* out);
void ili9341_line(int x0, int y0, int x1, int y1);

uint32_t lcd_send_command(uint8_t cmd, uint8_t len, const uint8_t *data);
void     lcd_setBrightness(uint16_t b);

// SD Card support, discio functions for FatFS lib implemented in ili9341.c
#ifdef  __USE_SD_CARD__
#include "../FatFs/ff.h"
#include "../FatFs/diskio.h"
void testLog(void);        // debug log
#endif

/*
 * rtc.c
 */
#ifdef __USE_RTC__
#define RTC_START_YEAR          2000

#define RTC_DR_YEAR(dr)         (((dr)>>16)&0xFF)
#define RTC_DR_MONTH(dr)        (((dr)>> 8)&0xFF)
#define RTC_DR_DAY(dr)          (((dr)>> 0)&0xFF)

#define RTC_TR_HOUR(dr)         (((tr)>>16)&0xFF)
#define RTC_TR_MIN(dr)          (((tr)>> 8)&0xFF)
#define RTC_TR_SEC(dr)          (((tr)>> 0)&0xFF)

// Init RTC
void rtc_init(void);
// Then read time and date TR should read first, after DR !!!
// Get RTC time as bcd structure in 0x00HHMMSS
#define rtc_get_tr_bcd() (RTC->TR & 0x007F7F7F)
// Get RTC date as bcd structure in 0x00YYMMDD (remove day of week information!!!!)
#define rtc_get_dr_bcd() (RTC->DR & 0x00FF1F3F)
// read TR as 0x00HHMMSS in bin (TR should be read first for sync)
uint32_t rtc_get_tr_bin(void);
// read DR as 0x00YYMMDD in bin (DR should be read second)
uint32_t rtc_get_dr_bin(void);
// Read time in FAT filesystem format
uint32_t rtc_get_FAT(void);
// Write date and time (need in bcd format!!!)
void rtc_set_time(uint32_t dr, uint32_t tr);
#endif

/*
 * flash.c
 */

#define FLASH_PAGESIZE 0x800

#define SAVEAREA_MAX 5

// Depend from config_t size, should be aligned by FLASH_PAGESIZE
#define SAVE_CONFIG_SIZE        0x00000800
// Depend from properties_t size, should be aligned by FLASH_PAGESIZE
#define SAVE_PROP_CONFIG_SIZE   0x00001800
// Save config_t and properties_t flash area (see flash7  : org = 0x08018000, len = 32k from *.ld settings)
// Properties save area follow after config
// len = SAVE_CONFIG_SIZE + SAVEAREA_MAX * SAVE_PROP_CONFIG_SIZE   0x00008000  32k
#define SAVE_CONFIG_ADDR        0x08018000
#define SAVE_PROP_CONFIG_ADDR   (SAVE_CONFIG_ADDR + SAVE_CONFIG_SIZE)
#define SAVE_FULL_AREA_SIZE     (SAVE_CONFIG_SIZE + SAVEAREA_MAX * SAVE_PROP_CONFIG_SIZE)

#define CONFIG_MAGIC 0x434f4e45 /* 'CONF' */

extern uint16_t lastsaveid;

#define frequency0 current_props._frequency0
#define frequency1 current_props._frequency1
#define sweep_points current_props._sweep_points
#define cal_status current_props._cal_status
#define cal_data active_props->_cal_data
#define electrical_delay current_props._electrical_delay

#define trace current_props._trace
#define markers current_props._markers
#define active_marker current_props._active_marker
#define domain_mode current_props._domain_mode
#define velocity_factor current_props._velocity_factor
#define marker_smith_format current_props._marker_smith_format

#define previous_marker uistat._previous_marker
#define current_trace   uistat._current_trace
#define FREQ_IS_STARTSTOP() (!(config._mode&VNA_MODE_CENTER_SPAN))
#define FREQ_IS_CENTERSPAN() (config._mode&VNA_MODE_CENTER_SPAN)
#define FREQ_IS_CW() (frequency0 == frequency1)

int caldata_save(uint32_t id);
int caldata_recall(uint32_t id);
const properties_t *caldata_reference(void);

int config_save(void);
int config_recall(void);

void clear_all_config_prop_data(void);

/*
 * ui.c
 */

// Obsolete value input variant
//#define UI_USE_NUMERIC_INPUT
// Enter in leveler search mode after search click
//#define UI_USE_LEVELER_SEARCH_MODE

void ui_init(void);
void ui_process(void);

void handle_touch_interrupt(void);

void touch_cal_exec(void);
void touch_draw_test(void);
void enter_dfu(void);

// Irq operation process set
#define OP_NONE       0x00
#define OP_LEVER      0x01
#define OP_TOUCH      0x02
#define OP_CONSOLE    0x04
extern volatile uint8_t operation_requested;

// lever_mode
enum lever_mode {
  LM_MARKER, LM_SEARCH, LM_CENTER, LM_SPAN, LM_EDELAY
};

#define MARKER_INVALID       -1
#define TRACE_INVALID        -1
typedef struct uistat {
//  uint32_t value;         // for editing at numeric input area
//  int8_t digit;           // 0~5 used in numeric input (disabled)
//  int8_t digit_mode;      // used in numeric input (disabled)
  int8_t  _current_trace;   // 0..(TRACES_MAX -1) (TRACE_INVALID  for disabled)
  int8_t  _previous_marker; // 0..(MARKERS_MAX-1) (MARKER_INVALID for disabled)
  uint8_t lever_mode;
  uint8_t marker_delta:1;
  uint8_t marker_tracking:1;
} uistat_t;

extern uistat_t uistat;

#define TOUCH_THRESHOLD 2000

/*
 * adc.c
 */
#define ADC_TOUCH_X  ADC_CHSELR_CHSEL6
#define ADC_TOUCH_Y  ADC_CHSELR_CHSEL7

void adc_init(void);
uint16_t adc_single_read(uint32_t chsel);
void adc_start_analog_watchdog(void);
void adc_stop_analog_watchdog(void);
int16_t adc_vbat_read(void);

/*
 * misclinous
 */
int plot_printf(char *str, int, const char *fmt, ...);
#define PULSE do { palClearPad(GPIOC, GPIOC_LED); palSetPad(GPIOC, GPIOC_LED);} while(0)

#define ARRAY_COUNT(a)    (sizeof(a)/sizeof(*(a)))
// Speed profile definition
#define START_PROFILE   systime_t time = chVTGetSystemTimeX();
#define STOP_PROFILE    {char string_buf[12];plot_printf(string_buf, sizeof string_buf, "T:%06d", chVTGetSystemTimeX() - time);ili9341_drawstringV(string_buf, 1, 60);}
// Macros for convert define value to string
#define STR1(x)  #x
#define define_to_STR(x)  STR1(x)
/*EOF*/
