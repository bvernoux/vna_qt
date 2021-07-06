//
// General type definitions for portability
// 
#ifndef US_TYPEDEFS
#define US_TYPEDEFS

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <float.h>
#include <malloc.h>

#include <algorithm>
using namespace std;

typedef signed   int     BOOL32;
typedef unsigned char    U8 ;
typedef unsigned short   U16;
typedef unsigned int     U32;
typedef          char    C8 ;
typedef signed   char    S8 ;
typedef signed   short   S16;
typedef signed   int     S32;

#ifdef _MSC_VER
#if (_MSC_VER >= 1500)
  typedef uintptr_t        UINTa;
  typedef intptr_t         SINTa;
#else
  typedef unsigned __int64 UINTa;
  typedef signed   __int64 SINTa;
#endif
#else
  typedef unsigned __int64 UINTa;
  typedef signed   __int64 SINTa;
#endif

#ifdef __cplusplus      // (define these in C++ only, for H2INC compatibility)
typedef unsigned __int64 U64;
typedef signed   __int64 S64;
#endif

#endif

#ifndef F_TYPEDEFS
#define F_TYPEDEFS

typedef float            SINGLE;
typedef float            F32;
typedef double           DOUBLE;
typedef double           F64;

#endif

#ifndef YES
#define YES 1
#endif

#ifndef NO
#define NO  0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE  0
#endif

//
// Abstracted type/compiler definitions for portability
//

#pragma once

//
// Subset of MSVC warnings promoted to errors so compile will fail even if /WX isn't used
//

#ifdef _WINDOWS_

#pragma warning (error:4265)        // Class has virtual functions but destructor is not virtual
#pragma warning (error:4035)        // No return value
#pragma warning (error:4715)        // Not all control paths return a value
#pragma warning (error:4013)        // Call to undefined function
#pragma warning (error:4700)        // Variable used before assignment
#pragma warning (error:4701)        // Variable may be used before assignment
#pragma warning (error:4005)        // Macro redefinition
#pragma warning (error:4098)        // void function returns a value
#pragma warning (error:4071)        // Call to unprototyped function
#pragma warning (error:4072)        // Call to unprototyped function
#pragma warning (error:4020)        // Too many parameters
#pragma warning (error:4554)        // Check operator precedence for possible error
#pragma warning (error:4296)        // Expression is always false

//
// Subset of MSVC warnings disabled entirely
//

#pragma warning (disable:4100)      // unreferenced formal parameter
#pragma warning (disable:4505)      // unreferenced local function
#pragma warning (disable:4512)      // assignment operator could not be generated due to const/& member
#pragma warning (disable:4702)      // unreachable code

//
// Enable a subset of off-by-default MSVC warnings
//

#pragma warning (default:4777)      // format string type mismatch

#endif

#ifndef strlen
#define strlen (int) strlen         // allow signed ints with strlen() without "size_t cast to 32-bit int" warning
#endif

#ifndef US_TYPEDEFS
#define US_TYPEDEFS

typedef unsigned char    U8 ;
typedef unsigned short   U16;
typedef unsigned int     U32;
typedef          char    C8 ;
typedef signed   char    S8 ;
typedef signed   short   S16;
typedef signed   int     S32;

#ifdef _MSC_VER
#if (_MSC_VER >= 1500)
  typedef uintptr_t        UINTa;
  typedef intptr_t         SINTa;
#else
  typedef unsigned __int64 UINTa;
  typedef signed   __int64 SINTa;
#endif
#else
#ifdef __AVR__
typedef unsigned long long UINTa;
typedef long long          SINTa;
#else
typedef unsigned __int64   UINTa;
typedef signed   __int64   SINTa;
#endif
#endif

#ifdef __cplusplus      // (define these in C++ only, for H2INC compatibility)
#ifdef __AVR__
typedef unsigned long long U64;
typedef long long          S64;
#else
typedef unsigned __int64   U64;
typedef signed   __int64   S64;
#endif
#endif

#endif

#ifndef F_TYPEDEFS
#define F_TYPEDEFS

typedef float            SINGLE;
typedef float            F32;
typedef double           DOUBLE;
typedef double           F64;

#endif

#ifndef C_TYPEDEFS
#define C_TYPEDEFS

#ifndef FREE
#define FREE(x) if ((x) != NULL) { free((x)); (x) = NULL; }
#endif

#ifndef ARY_CNT
#define ARY_CNT(x) sizeof(x) / sizeof((x)[0])
#endif

#ifndef PI_DEFINED
#define PI_DEFINED
#ifndef PI
   static const DOUBLE PI          = 3.14159265358979323846;
#endif
#ifndef TWO_PI
   static const DOUBLE TWO_PI      = (2.0 * PI);
#endif
   static const DOUBLE PI_OVER_TWO = (0.5 * PI);
   static const DOUBLE LN_TWO      = 0.69314718055994530942;

   #ifndef RAD2DEG_DEFINED
   #define RAD2DEG_DEFINED
      static const DOUBLE RAD2DEG = 180.0 / PI;
      static const DOUBLE DEG2RAD = PI / 180.0;
   #endif
#endif

#ifdef __cplusplus

struct COMPLEX_DOUBLE      // No virtuals, so instances can be allocated with malloc() or new
{
   DOUBLE real;
   DOUBLE imag;

   COMPLEX_DOUBLE(DOUBLE r, DOUBLE i) : real(r), imag(i)
      {
      }

   COMPLEX_DOUBLE(DOUBLE r) : real(r), imag(0.0)
      {
      }

   COMPLEX_DOUBLE() : real(0.0), imag(0.0)      // (allows { 0 } initialization to work)
      {
      }

#ifdef _INC_MATH
   const DOUBLE cabs(void)
      {
      return hypot(real, imag);
      }

   static const DOUBLE cabs(COMPLEX_DOUBLE val)
      {
      return val.hypot();
      }

   static DOUBLE hypot(DOUBLE x, DOUBLE y)
      {
      x = fabs(x);
      y = fabs(y);

      if (x < 1E-30) return y;
      if (y < 1E-30) return x;

      DOUBLE t = (x < y) ? x : y;
      x = (x > y) ? x : y;

      t /= x;

      return x * sqrt(1.0 + (t*t));
      }

   const DOUBLE hypot(void)
      {
      return hypot(real, imag);
      }

   const DOUBLE carg(void)
      {
      return atan2(imag, real);
      }

   const DOUBLE cmag(void)
      {
      return sqrt(real*real+imag*imag);
      }

   const COMPLEX_DOUBLE cpow(COMPLEX_DOUBLE pwr)
      {
      if ((pwr.real == 0.0) && (pwr.imag == 0.0))
         {
         return COMPLEX_DOUBLE(1.0, 0.0);
         }

      if ((real == 0.0) && (imag == 0.0))
         {
         return COMPLEX_DOUBLE(0.0, 0.0);
         }

      DOUBLE vabs  = hypot(real, imag);
      DOUBLE len   = pow(vabs, pwr.real);
      DOUBLE at    = atan2(imag, real);
      DOUBLE phase = at * pwr.real;

      if (pwr.imag != 0.0)
         {
         len /= exp(at * pwr.imag);
         phase += pwr.imag * log(vabs);
         }

      return COMPLEX_DOUBLE(len * cos(phase), len * sin(phase));
      }

   static const COMPLEX_DOUBLE cpow(COMPLEX_DOUBLE val, COMPLEX_DOUBLE pwr)
      {
      return val.cpow(pwr);
      }

   const COMPLEX_DOUBLE csqrt(void)
      {
      return cpow(0.5);
      }

   static COMPLEX_DOUBLE csqrt(COMPLEX_DOUBLE val)
      {
      return val.csqrt();
      }
#endif

   const COMPLEX_DOUBLE conj(void)
      {
      COMPLEX_DOUBLE temp;

      temp.real =  real;
      temp.imag = -imag;

      return temp;
      }

   const bool operator == (const COMPLEX_DOUBLE &c) const
      {
      return (real == c.real) && (imag == c.imag);
      }

   const COMPLEX_DOUBLE operator + (const COMPLEX_DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real + c.real;
      temp.imag = imag + c.imag;

      return temp;
      }

   const COMPLEX_DOUBLE operator - (const COMPLEX_DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real - c.real;
      temp.imag = imag - c.imag;

      return temp;
      }

   const COMPLEX_DOUBLE operator * (const COMPLEX_DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = (real * c.real) - (imag * c.imag);
      temp.imag = (real * c.imag) + (imag * c.real);

      return temp;
      }

   const COMPLEX_DOUBLE operator / (const COMPLEX_DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      DOUBLE D = (c.real * c.real) + (c.imag * c.imag);

      temp.real = ((real * c.real) + (imag * c.imag)) / D;
      temp.imag = ((imag * c.real) - (real * c.imag)) / D;

      return temp;
      }

   const COMPLEX_DOUBLE operator + (const DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real + c;
      temp.imag = imag + c;

      return temp;
      }

   const COMPLEX_DOUBLE operator - (const DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real - c;
      temp.imag = imag - c;

      return temp;
      }

   const COMPLEX_DOUBLE operator * (const DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real * c;
      temp.imag = imag * c;

      return temp;
      }

   const COMPLEX_DOUBLE operator / (const DOUBLE &c) const
      {
      COMPLEX_DOUBLE temp;

      temp.real = real / c;
      temp.imag = imag / c;

      return temp;
      }
};

#else

typedef struct _COMPLEX_DOUBLE
{
   DOUBLE real;
   DOUBLE imag;
}
COMPLEX_DOUBLE;

#endif
#endif

#ifndef YES
#define YES 1
#endif

#ifndef NO
#define NO  0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE  0
#endif

#ifdef ANSI_CONSOLE_COLORS
#define AC_GRAY      "\x1b[01m\x1b[30m"
#define AC_RED       "\x1b[01m\x1b[31m"
#define AC_DKRED     "\x1b[00m\x1b[31m"
#define AC_GREEN     "\x1b[01m\x1b[32m"
#define AC_DKGREEN   "\x1b[00m\x1b[32m"
#define AC_YELLOW    "\x1b[01m\x1b[33m"
#define AC_DKYELLOW  "\x1b[00m\x1b[33m"
#define AC_BLUE      "\x1b[01m\x1b[34m"
#define AC_DKBLUE    "\x1b[00m\x1b[34m"
#define AC_MAGENTA   "\x1b[01m\x1b[35m"
#define AC_DKMAGENTA "\x1b[00m\x1b[35m"
#define AC_CYAN      "\x1b[01m\x1b[36m"
#define AC_DKCYAN    "\x1b[00m\x1b[36m"
#define AC_WHITE     "\x1b[01m\x1b[37m"
#define AC_DKWHITE   "\x1b[00m\x1b[37m"
#define AC_RESET     "\x1b[01m\x1b[0m"
#define AC_NORM      "\x1b[01m\x1b[0m"
#define AC_NORMAL    "\x1b[01m\x1b[0m"
#else
#ifdef NO_ANSI_CONSOLE_COLORS
#endif
#define AC_GRAY      ""
#define AC_RED       ""
#define AC_DKRED     ""
#define AC_GREEN     ""
#define AC_DKGREEN   ""
#define AC_YELLOW    ""
#define AC_DKYELLOW  ""
#define AC_BLUE      ""
#define AC_DKBLUE    ""
#define AC_MAGENTA   ""
#define AC_DKMAGENTA ""
#define AC_CYAN      ""
#define AC_DKCYAN    ""
#define AC_WHITE     ""
#define AC_DKWHITE   ""
#define AC_RESET     ""
#define AC_NORM      ""
#define AC_NORMAL    ""
#endif

#ifndef NO_BINARY_CONSTANTS
#define B00000000 0
#define B00000001 1
#define B00000010 2
#define B00000011 3
#define B00000100 4
#define B00000101 5
#define B00000110 6
#define B00000111 7
#define B00001000 8
#define B00001001 9
#define B00001010 10
#define B00001011 11
#define B00001100 12
#define B00001101 13
#define B00001110 14
#define B00001111 15
#define B00010000 16
#define B00010001 17
#define B00010010 18
#define B00010011 19
#define B00010100 20
#define B00010101 21
#define B00010110 22
#define B00010111 23
#define B00011000 24
#define B00011001 25
#define B00011010 26
#define B00011011 27
#define B00011100 28
#define B00011101 29
#define B00011110 30
#define B00011111 31
#define B00100000 32
#define B00100001 33
#define B00100010 34
#define B00100011 35
#define B00100100 36
#define B00100101 37
#define B00100110 38
#define B00100111 39
#define B00101000 40
#define B00101001 41
#define B00101010 42
#define B00101011 43
#define B00101100 44
#define B00101101 45
#define B00101110 46
#define B00101111 47
#define B00110000 48
#define B00110001 49
#define B00110010 50
#define B00110011 51
#define B00110100 52
#define B00110101 53
#define B00110110 54
#define B00110111 55
#define B00111000 56
#define B00111001 57
#define B00111010 58
#define B00111011 59
#define B00111100 60
#define B00111101 61
#define B00111110 62
#define B00111111 63
#define B01000000 64
#define B01000001 65
#define B01000010 66
#define B01000011 67
#define B01000100 68
#define B01000101 69
#define B01000110 70
#define B01000111 71
#define B01001000 72
#define B01001001 73
#define B01001010 74
#define B01001011 75
#define B01001100 76
#define B01001101 77
#define B01001110 78
#define B01001111 79
#define B01010000 80
#define B01010001 81
#define B01010010 82
#define B01010011 83
#define B01010100 84
#define B01010101 85
#define B01010110 86
#define B01010111 87
#define B01011000 88
#define B01011001 89
#define B01011010 90
#define B01011011 91
#define B01011100 92
#define B01011101 93
#define B01011110 94
#define B01011111 95
#define B01100000 96
#define B01100001 97
#define B01100010 98
#define B01100011 99
#define B01100100 100
#define B01100101 101
#define B01100110 102
#define B01100111 103
#define B01101000 104
#define B01101001 105
#define B01101010 106
#define B01101011 107
#define B01101100 108
#define B01101101 109
#define B01101110 110
#define B01101111 111
#define B01110000 112
#define B01110001 113
#define B01110010 114
#define B01110011 115
#define B01110100 116
#define B01110101 117
#define B01110110 118
#define B01110111 119
#define B01111000 120
#define B01111001 121
#define B01111010 122
#define B01111011 123
#define B01111100 124
#define B01111101 125
#define B01111110 126
#define B01111111 127
#define B10000000 128
#define B10000001 129
#define B10000010 130
#define B10000011 131
#define B10000100 132
#define B10000101 133
#define B10000110 134
#define B10000111 135
#define B10001000 136
#define B10001001 137
#define B10001010 138
#define B10001011 139
#define B10001100 140
#define B10001101 141
#define B10001110 142
#define B10001111 143
#define B10010000 144
#define B10010001 145
#define B10010010 146
#define B10010011 147
#define B10010100 148
#define B10010101 149
#define B10010110 150
#define B10010111 151
#define B10011000 152
#define B10011001 153
#define B10011010 154
#define B10011011 155
#define B10011100 156
#define B10011101 157
#define B10011110 158
#define B10011111 159
#define B10100000 160
#define B10100001 161
#define B10100010 162
#define B10100011 163
#define B10100100 164
#define B10100101 165
#define B10100110 166
#define B10100111 167
#define B10101000 168
#define B10101001 169
#define B10101010 170
#define B10101011 171
#define B10101100 172
#define B10101101 173
#define B10101110 174
#define B10101111 175
#define B10110000 176
#define B10110001 177
#define B10110010 178
#define B10110011 179
#define B10110100 180
#define B10110101 181
#define B10110110 182
#define B10110111 183
#define B10111000 184
#define B10111001 185
#define B10111010 186
#define B10111011 187
#define B10111100 188
#define B10111101 189
#define B10111110 190
#define B10111111 191
#define B11000000 192
#define B11000001 193
#define B11000010 194
#define B11000011 195
#define B11000100 196
#define B11000101 197
#define B11000110 198
#define B11000111 199
#define B11001000 200
#define B11001001 201
#define B11001010 202
#define B11001011 203
#define B11001100 204
#define B11001101 205
#define B11001110 206
#define B11001111 207
#define B11010000 208
#define B11010001 209
#define B11010010 210
#define B11010011 211
#define B11010100 212
#define B11010101 213
#define B11010110 214
#define B11010111 215
#define B11011000 216
#define B11011001 217
#define B11011010 218
#define B11011011 219
#define B11011100 220
#define B11011101 221
#define B11011110 222
#define B11011111 223
#define B11100000 224
#define B11100001 225
#define B11100010 226
#define B11100011 227
#define B11100100 228
#define B11100101 229
#define B11100110 230
#define B11100111 231
#define B11101000 232
#define B11101001 233
#define B11101010 234
#define B11101011 235
#define B11101100 236
#define B11101101 237
#define B11101110 238
#define B11101111 239
#define B11110000 240
#define B11110001 241
#define B11110010 242
#define B11110011 243
#define B11110100 244
#define B11110101 245
#define B11110110 246
#define B11110111 247
#define B11111000 248
#define B11111001 249
#define B11111010 250
#define B11111011 251
#define B11111100 252
#define B11111101 253
#define B11111110 254
#define B11111111 255
#endif

//
// Quick and dirty memory leak detection
//

#ifdef MEMDEBUG
#ifdef __cplusplus

struct MDHDR
{
   char           file[256];
   int            line;
   size_t         num;
   size_t         size;

   void          *user;
   struct MDHDR **link;
};

static const int MAX_MDH = 65536;
static MDHDR *MDH[MAX_MDH];

struct MDHDUMP
{
   ~MDHDUMP()
      {
      for (int i=0; i < MAX_MDH; i++)
         {
         MDHDR *H = MDH[i];
         if (H == NULL) continue;

         printf("%s %d: %d bytes leaked\n", H->file, H->line, (int) (H->num * H->size));
         }
      }
};

static MDHDUMP MDH_dump;

void *MDH_alloc(size_t num, size_t size, bool clear, char const *file, int line)
{
   size_t bytes = num*size;

   bytes += sizeof(MDHDR);

   void *entry = (void *) malloc(bytes);
   assert(entry);

   if (clear)
      {
      memset(entry, 0, bytes);
      }

   MDHDR *H = (MDHDR *) entry;
   memset(H, 0, sizeof(*H));

   _snprintf(H->file,sizeof(H->file)-1,"%s",file);
   H->line = line;

   H->user = (void *) ((U8 *) entry + sizeof(MDHDR));

   H->num = num;
   H->size = size;

   for (int i=0; i < MAX_MDH; i++)
      {
      if (MDH[i] == NULL)
         {
         H->link = &MDH[i];
         break;
         }
      }

   if (H->link != NULL)
      {
      *H->link = H;
      }

   return H->user;
}

void MDH_free(void *user)
{
   MDHDR *H = ((MDHDR *) user) - 1;

   if (H->link != NULL)
      {
      assert(*H->link == H);
      *H->link = NULL;
      }

   free(H);
}

#define DBG_CALLOC(x,y) MDH_alloc(x, y, true,  __FILE__, __LINE__)
#define DBG_MALLOC(x)   MDH_alloc(x, 1, false, __FILE__, __LINE__)

#undef calloc
#undef malloc
#undef free

#define calloc(x,y)  DBG_CALLOC(x,y)
#define malloc(x)    DBG_MALLOC(x)
#define free(x)      MDH_free(x)

void *operator new(size_t size, char const *file, int line)
{
   return MDH_alloc(size, 1, true, file, line);
}

void *operator new(size_t size)
{
   return MDH_alloc(size, 1, false, __FILE__, __LINE__);
}

void operator delete(void *user)
{
   MDH_free(user);
}

#undef new
#define new new(__FILE__, __LINE__)

#endif
#endif
