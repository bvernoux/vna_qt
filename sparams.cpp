/*********************************************************************/
//
// S-parameter storage class, including .SNP file access, serialization,
// interpolation, and caching
//
// john@miles.io
//  
/*********************************************************************/
#include "typedefs.h"

#define MAX_PATH (260)

namespace SNPTYPE       // Flags used to indicate which format(s) are cached in database
{
    const U8 MA = 0x01;  // Magnitude-angle form is valid
    const U8 DB = 0x02;  // dB-angle form is valid
    const U8 RI = 0x04;  // Real-imag form is valid
    const U8 CZ = 0x08;  // Complex impedance (R+jX) is valid (conversion based on real part of Zo)
}

namespace SPARAM
{
    struct MA
    {
        DOUBLE mag;
        DOUBLE deg;

        MA(DOUBLE m, DOUBLE a) : mag(m), deg(a) {}
        MA(struct DB);
        MA(struct RI);
    };

    struct DB
    {
        DOUBLE dB;
        DOUBLE deg;

        DB(DOUBLE d, DOUBLE a) : dB(d), deg(a) {}
        DB(struct MA);
        DB(struct RI);
    };

    struct RI : public COMPLEX_DOUBLE
    {
        RI(DOUBLE r, DOUBLE i) : COMPLEX_DOUBLE(r, i) {}
        RI(COMPLEX_DOUBLE c) : COMPLEX_DOUBLE(c)   {}
        RI(struct MA);
        RI(struct DB);
    };

    struct CZ
    {
        DOUBLE R;
        DOUBLE jX;

        CZ(DOUBLE r, DOUBLE x) : R(r), jX(x) {}
        CZ(struct MA, DOUBLE Ro);
    };

    MA::MA(DB dB) : mag(pow(10.0, dB.dB / 20.0)),
            deg(dB.deg)
    {
    }

    MA::MA(RI RI)
    {
        DOUBLE i = RI.real;
        DOUBLE q = RI.imag;

        mag = sqrt(i*i + q*q);

        if (mag > 1E-20)
                deg = atan2(q, i) * RAD2DEG;
        else
                deg = 0.0;
    }

    RI::RI(MA MA)
    {
        DOUBLE ang = MA.deg * DEG2RAD;
        DOUBLE mag = MA.mag;

        real = cos(ang) * mag;
        imag = sin(ang) * mag;
    }

    RI::RI(DB DB)
    {
        DOUBLE ang = DB.deg * DEG2RAD;
        DOUBLE mag = pow(10.0, DB.dB / 20.0);

        real = cos(ang) * mag;
        imag = sin(ang) * mag;
    }

    DB::DB(MA MA)
    {
        dB = 20.0 * log10(max(1E-15, MA.mag));
        deg = MA.deg;
    }

    DB::DB(RI RI)
    {
        DOUBLE i = RI.real;
        DOUBLE q = RI.imag;

        dB = 20.0 * log10(max(1E-15, sqrt(i*i + q*q)));

        if (dB > -200.0)
                deg = atan2(q, i) * RAD2DEG;
        else
                deg = 0.0;
    }

    CZ::CZ(MA MA, DOUBLE Ro)
    {
        DOUBLE mm = MA.mag * MA.mag;     // (www.microwaves101.com/encyclopedias/s-parameter-utilities-spreadsheet#smith)
        DOUBLE r = MA.deg * DEG2RAD;

        R = ((1.0 - mm)            * Ro) / (1.0 + mm - (2.0 * MA.mag * cos(r)));
        jX = (2.0 * MA.mag * sin(r) * Ro) / (1.0 + mm - (2.0 * MA.mag * cos(r)));
    }

    enum MSGLVL
    {
        MSG_DEBUG = 0,    // Debugging traffic
        MSG_VERBOSE,      // Low-level notice
        MSG_NOTICE,       // Standard notice
        MSG_WARNING,      // Warning
        MSG_ERROR         // Error
    };

    const U8 EXT_ZERO = 0x01;        // Frequency-based queries outside min/max range return valid zero magnitude and phase
    const U8 EXT_LEND = 0x02;        // Frequency-based queries below min return valid min endpoint
    const U8 EXT_REND = 0x04;        // Frequency-based queries above max return valid max endpoint
    const U8 EXT_ENDS = EXT_LEND | EXT_REND;

    const U32 BIN_ID = 'BPNS';      // Binary stream identifier 'SNPB' (little-endian)
    const U32 BIN_VERSION = 0x00000001;  // Binary stream version written by this implementation

    const C8 *DEF_DATA_FORMAT = "MA";        // Default format for .S2P file writes
    const C8 *DEF_FREQ_FORMAT = "GHZ";
}

struct SPARAMS
{
    C8             message_text[4096];     // Error/warning text buffer for optional app access

    S32            n_ports;                // Matrix dimensions S[m][m], currently must be either 1 or 2
    S32            n_points;

    DOUBLE         min_Hz;                 // Valid after read_SNP_file() or application-specific setup
    DOUBLE         max_Hz;                 // Application can access these variables (and the arrays below) directly
    COMPLEX_DOUBLE Zo;

    DOUBLE        *freq_Hz;                // [n_points]
    U8          ***valid;                  // [b][a][n_points]
    SPARAM::MA  ***MA;                     // [b][a][n_points]
    SPARAM::DB  ***DB;
    SPARAM::RI  ***RI;
    SPARAM::CZ  ***CZ;

    // --------------------------------------------------------------------------------------------------
    // Error/status message sink can be subclassed if desired
    // to redirect output
    // --------------------------------------------------------------------------------------------------

    virtual void message_sink(SPARAM::MSGLVL level,
            C8            *text)
    {
        Q_UNUSED(level);
        ::printf("%s\n", text);
    }

    virtual void message_printf(SPARAM::MSGLVL level,
            C8            *fmt,
            ...)
    {
        Q_UNUSED(level);
        va_list ap;

        va_start(ap,
                fmt);

        _vsnprintf(message_text,
                sizeof(message_text) - 1,
                fmt,
                ap);

        va_end(ap);

        //
        // Remove trailing whitespace
        //
        C8 *end = &message_text[strlen(message_text) - 1];

        while (end > message_text)
        {
            if (!isspace((U8)*end))
            {
                break;
            }

            *end = 0;
            end--;
        }

        message_sink(level,
                message_text);
    }

    // --------------------------------------------------------------------------------------------------
    // Construction/destruction
    // --------------------------------------------------------------------------------------------------
    SPARAMS()
    {
        init();
    }

    virtual ~SPARAMS()
    {
        clear();
    }

    // --------------------------------------------------------------------------------------------------
    // Set construction defaults
    // --------------------------------------------------------------------------------------------------
    virtual void init(void)
    {
        memset(message_text, 0, sizeof(message_text));

        n_ports = 0;
        n_points = 0;
        min_Hz = DBL_MAX;
        max_Hz = -DBL_MAX;
        Zo = 50.0;
        valid = NULL;
        freq_Hz = NULL;
        MA = NULL;
        DB = NULL;
        RI = NULL;
        CZ = NULL;
    }

    // --------------------------------------------------------------------------------------------------
    // Discard existing database
    // --------------------------------------------------------------------------------------------------
    virtual void clear(void)
    {
        FREE(freq_Hz);

        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                if ((valid != NULL) && (valid[b] != NULL)) FREE(valid[b][a]);

                if ((MA != NULL) && (MA[b] != NULL)) FREE(MA[b][a]);
                if ((DB != NULL) && (DB[b] != NULL)) FREE(DB[b][a]);
                if ((RI != NULL) && (RI[b] != NULL)) FREE(RI[b][a]);
                if ((CZ != NULL) && (CZ[b] != NULL)) FREE(CZ[b][a]);
            }

            if (valid != NULL) FREE(valid[b]);
            if (MA != NULL)    FREE(MA[b]);
            if (DB != NULL)    FREE(DB[b]);
            if (RI != NULL)    FREE(RI[b]);
            if (CZ != NULL)    FREE(CZ[b]);
        }

        FREE(valid);
        FREE(MA);
        FREE(DB);
        FREE(RI);
        FREE(CZ);

        n_ports = 0;
        n_points = 0;
    }

    // --------------------------------------------------------------------------------------------------
    // Reserve specified number of ports (typically 1 or 2) and data points
    // --------------------------------------------------------------------------------------------------
    virtual bool alloc(S32 ports, S32 points)
    {
        if ((points == 0) || (ports == 0))
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Empty data set");
            return FALSE;
        }

        if ((n_ports != 0) || (n_points != 0))
        {
            clear();
        }

        n_ports = ports;
        n_points = points;

        freq_Hz = (DOUBLE *)calloc(n_points, sizeof(freq_Hz[0]));

        if (freq_Hz == NULL)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
            return FALSE;
        }

        valid = (U8 ***)calloc(n_points, sizeof(valid[0]));

        MA = (SPARAM::MA ***) calloc(n_ports, sizeof(MA[0]));
        DB = (SPARAM::DB ***) calloc(n_ports, sizeof(DB[0]));
        RI = (SPARAM::RI ***) calloc(n_ports, sizeof(RI[0]));
        CZ = (SPARAM::CZ ***) calloc(n_ports, sizeof(CZ[0]));

        if ((valid == NULL) || (MA == NULL) || (DB == NULL) || (RI == NULL) || (CZ == NULL))
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
            return FALSE;
        }

        for (S32 b = 0; b < n_ports; b++)
        {
            valid[b] = (U8 **)calloc(n_ports, sizeof(valid[0][0]));

            MA[b] = (SPARAM::MA **) calloc(n_ports, sizeof(MA[0][0]));
            DB[b] = (SPARAM::DB **) calloc(n_ports, sizeof(DB[0][0]));
            RI[b] = (SPARAM::RI **) calloc(n_ports, sizeof(RI[0][0]));
            CZ[b] = (SPARAM::CZ **) calloc(n_ports, sizeof(CZ[0][0]));

            if ((valid[b] == NULL) || (MA[b] == NULL) || (DB[b] == NULL) || (RI[b] == NULL) || (CZ[b] == NULL))
            {
                message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
                return FALSE;
            }

            for (S32 a = 0; a < n_ports; a++)
            {
                valid[b][a] = (U8 *)calloc(n_points, sizeof(valid[0][0][0]));

                MA[b][a] = (SPARAM::MA *) calloc(n_points, sizeof(MA[0][0][0]));
                DB[b][a] = (SPARAM::DB *) calloc(n_points, sizeof(DB[0][0][0]));
                RI[b][a] = (SPARAM::RI *) calloc(n_points, sizeof(RI[0][0][0]));
                CZ[b][a] = (SPARAM::CZ *) calloc(n_points, sizeof(CZ[0][0][0]));

                if ((valid[b][a] == NULL) || (MA[b][a] == NULL) || (DB[b][a] == NULL) || (RI[b][a] == NULL) || (CZ[b][a] == NULL))
                {
                    message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
                    return FALSE;
                }
            }
        }
        return TRUE;
    }

    // --------------------------------------------------------------------------------------------------
    // Serialize to binary block
    //
    //   Header: U8  identifier[4] 'SNPB'
    //           U32 version
    //           S32 n_data_bytes (not including header)
    //
    // Contents: ...
    //           Version-specific data
    //           ...
    //
    // Caller must free the returned block
    // --------------------------------------------------------------------------------------------------

    virtual U8 *serialize(S32 *output_bytes)
    {
        S32 n_data_bytes = 0;

        n_data_bytes += sizeof(n_ports);
        n_data_bytes += sizeof(n_points);
        n_data_bytes += sizeof(min_Hz);
        n_data_bytes += sizeof(max_Hz);
        n_data_bytes += sizeof(Zo);

        n_data_bytes += n_points * sizeof(freq_Hz[0]);

        S32 mat_size = n_ports * n_ports * n_points;      // 1 for S1P, 4 for S2P, 9 for S3P...

        n_data_bytes += mat_size * sizeof(valid[0][0][0]);
        n_data_bytes += mat_size * sizeof(MA[0][0][0]);
        n_data_bytes += mat_size * sizeof(DB[0][0][0]);
        n_data_bytes += mat_size * sizeof(RI[0][0][0]);
        n_data_bytes += mat_size * sizeof(CZ[0][0][0]);

        S32 n_block_bytes = sizeof(SPARAM::BIN_ID) +
                sizeof(SPARAM::BIN_VERSION) +
                sizeof(n_data_bytes) +
                n_data_bytes;

        U8 *block = (U8 *)malloc(n_block_bytes);

        if (block == NULL)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
            return NULL;
        }

        U8 *block_start = block;

        memcpy(block, &SPARAM::BIN_ID, sizeof(SPARAM::BIN_ID));      block += sizeof(SPARAM::BIN_ID);
        memcpy(block, &SPARAM::BIN_VERSION, sizeof(SPARAM::BIN_VERSION)); block += sizeof(SPARAM::BIN_VERSION);
        memcpy(block, &n_data_bytes, sizeof(n_data_bytes));        block += sizeof(n_data_bytes);

        memcpy(block, &n_ports, sizeof(n_ports));   block += sizeof(n_ports);
        memcpy(block, &n_points, sizeof(n_points));  block += sizeof(n_points);
        memcpy(block, &min_Hz, sizeof(min_Hz));    block += sizeof(min_Hz);
        memcpy(block, &max_Hz, sizeof(max_Hz));    block += sizeof(max_Hz);
        memcpy(block, &Zo, sizeof(Zo));        block += sizeof(Zo);

        memcpy(block, &freq_Hz[0], n_points * sizeof(freq_Hz[0])); block += n_points * sizeof(freq_Hz[0]);

        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                memcpy(block, &valid[b][a][0], n_points * sizeof(valid[b][a][0])); block += n_points * sizeof(valid[b][a][0]);
                memcpy(block, &MA[b][a][0], n_points * sizeof(MA[b][a][0])); block += n_points * sizeof(MA[b][a][0]);
                memcpy(block, &DB[b][a][0], n_points * sizeof(DB[b][a][0])); block += n_points * sizeof(DB[b][a][0]);
                memcpy(block, &RI[b][a][0], n_points * sizeof(RI[b][a][0])); block += n_points * sizeof(RI[b][a][0]);
                memcpy(block, &CZ[b][a][0], n_points * sizeof(CZ[b][a][0])); block += n_points * sizeof(CZ[b][a][0]);
            }
        }

        assert(block - block_start == n_block_bytes);

        *output_bytes = n_block_bytes;
        return block_start;
    }

    // --------------------------------------------------------------------------------------------------
    // Deserialize from open binary file handle
    //
    // Returns # of bytes processed or -1 on error
    // (0 = file contents not recognized as a serialized .S2P file)
    // --------------------------------------------------------------------------------------------------

    virtual S32 deserialize(FILE *in)
    {
#pragma pack(push,1)
        struct _HDR
        {
            U32 ID;
            U32 version;
            S32 n_data_bytes;
        }
        HDR;
#pragma pack(pop)

        //
        // Read first four bytes of non-version-specific header
        //

        if (fread(&HDR.ID, sizeof(HDR.ID), 1, in) != 1)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Couldn't read from SNPB file");
            return -1;
        }

        if (HDR.ID != SPARAM::BIN_ID)
        {
            //
            // This isn't our block -- rewind the file pointer and return without reporting an error
            //

            fseek(in, -(S32) sizeof(HDR.ID), SEEK_CUR);
            message_printf(SPARAM::MSG_VERBOSE, (C8*)"Unrecognized block ID");
            return 0;
        }

        //
        // Read rest of header, followed by data block
        //

        if (fread(&HDR.version, sizeof(HDR.version) + sizeof(HDR.n_data_bytes), 1, in) != 1)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Couldn't read from SNPB file");
            return -1;
        }

        U8 *block = (U8 *)malloc(HDR.n_data_bytes + sizeof(HDR));

        if (block == NULL)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Out of memory");
            return -1;
        }

        U8 *block_start = block;

        memcpy(block, &HDR, sizeof(HDR)); block += sizeof(HDR);

        if (fread(block, HDR.n_data_bytes, 1, in) != 1)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Couldn't read data from SNPB file");
            return -1;
        }

        S32 result = deserialize(block_start);

        FREE(block_start);

        return result;
    }

    // --------------------------------------------------------------------------------------------------
    // Deserialize from memory block
    //
    // Returns # of bytes processed or -1 on error
    // (0 = data not recognized as a serialized .S2P file)
    // --------------------------------------------------------------------------------------------------
    virtual S32 deserialize(U8 *block)
    {
        U32 read_ID = *(U32 *)block;

        if (read_ID != SPARAM::BIN_ID)
        {
            //
            // This isn't our block -- return without fetching any data or reporting an error
            //
            message_printf(SPARAM::MSG_VERBOSE, (C8*)"Unrecognized block ID");
            return 0;
        }

        clear();
        init();

        U8 *block_start = block;
        block += sizeof(read_ID);

        U32 read_version = *(U32 *)block; block += sizeof(read_version);

        if (read_version != SPARAM::BIN_VERSION)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Binary version 0x.08X not supported by version 0x.08X parser", read_version, SPARAM::BIN_VERSION);
            return -1;
        }

        S32 expect_n_data_bytes = *(S32 *)block; block += sizeof(expect_n_data_bytes);

        U8 *data_start = block;

        S32 read_n_ports = *(S32 *)block; block += sizeof(read_n_ports);
        S32 read_n_points = *(S32 *)block; block += sizeof(read_n_points);

        min_Hz = *(DOUBLE *)block; block += sizeof(min_Hz);
        max_Hz = *(DOUBLE *)block; block += sizeof(max_Hz);
        Zo = *(COMPLEX_DOUBLE *)block; block += sizeof(Zo);

        if (alloc(read_n_ports, read_n_points))
        {
            memcpy(&freq_Hz[0], block, n_points * sizeof(freq_Hz[0])); block += n_points * sizeof(freq_Hz[0]);

            for (S32 b = 0; b < n_ports; b++)
            {
                for (S32 a = 0; a < n_ports; a++)
                {
                    memcpy(&valid[b][a][0], block, n_points * sizeof(valid[b][a][0])); block += n_points * sizeof(valid[b][a][0]);
                    memcpy(&MA[b][a][0], block, n_points * sizeof(MA[b][a][0])); block += n_points * sizeof(MA[b][a][0]);
                    memcpy(&DB[b][a][0], block, n_points * sizeof(DB[b][a][0])); block += n_points * sizeof(DB[b][a][0]);
                    memcpy(&RI[b][a][0], block, n_points * sizeof(RI[b][a][0])); block += n_points * sizeof(RI[b][a][0]);
                    memcpy(&CZ[b][a][0], block, n_points * sizeof(CZ[b][a][0])); block += n_points * sizeof(CZ[b][a][0]);
                }
            }

            S32 bytes_read = (S32)(block - data_start);

            if (bytes_read != expect_n_data_bytes)
            {
                message_printf(SPARAM::MSG_ERROR, (C8*)"Missing or corrupt binary SNP data (%d bytes expected, %d read)",
                        expect_n_data_bytes,
                        bytes_read);

                return -1;
            }
        }
        return (S32)(block - block_start);
    }

    // --------------------------------------------------------------------------------------------------
    // Remove non-Touchstone compatible characters from string
    // --------------------------------------------------------------------------------------------------
    virtual C8 *sanitize(const C8 *input)
    {
        static C8 output[MAX_PATH];

        C8 *ptr = output;

        S32 len = strlen(input);

        if (len >= sizeof(output))
        {
            len = sizeof(output) - 1;
        }

        for (S32 i = 0; i < len; i++)
        {
            U8 s = input[i];

            if ((s < 0x20) || (s > 0x7E))
            {
                if ((s != 9) && (s != 10) && (s != 13))
                {
                    s = ' ';
                }
            }

            *ptr++ = (C8)s;
        }

        *ptr++ = 0;

        return output;
    }

    // --------------------------------------------------------------------------------------------------
    // Find data point closest to the specified frequency, or -1 if out of range
    //
    // alpha=0.0 if point matches freq_Hz[result] (or is an endpoint),
    // 1.0 if point matches freq_Hz[result+1]
    // --------------------------------------------------------------------------------------------------
    static int search_double_array(const void *keyval, const void *datum)
    {
        DOUBLE key = *(DOUBLE *)keyval;

        if (key < ((DOUBLE *)datum)[0]) return -1;
        if (key >= ((DOUBLE *)datum)[1]) return  1;

        return 0;
    }

    S32 nearest_freq_Hz(DOUBLE Hz, DOUBLE *alpha = NULL)
    {
        if (Hz <= freq_Hz[0])
        {
            if (alpha != NULL) *alpha = 0.0;
            return 0;
        }

        if (Hz >= freq_Hz[n_points - 1])
        {
            if (alpha != NULL) *alpha = 0.0;
            return n_points - 1;
        }

        //
        // Find index where freq_Hz[i+1] < Hz > freq_Hz[i]
        //
        DOUBLE *ptr = (DOUBLE *)bsearch(&Hz,
                freq_Hz,
                n_points - 1,
                sizeof(DOUBLE),
                search_double_array);
        assert(ptr != NULL);

        //
        // Return fractional result in alpha
        //
        S32 result = (S32)(ptr - freq_Hz);

        if (alpha != NULL)
        {
            *alpha = (Hz - freq_Hz[result]) / (freq_Hz[result + 1] - freq_Hz[result]);
        }

        return result;
    }

    // --------------------------------------------------------------------------------------------------
    // Return TRUE if value at specified point is available in any format
    // --------------------------------------------------------------------------------------------------
    virtual bool point_valid(S32 pt, S32 param)
    {
        const S32 b[4] = { 1, 2, 1, 2 };
        const S32 a[4] = { 1, 1, 2, 2 };

        assert(param < 4);
        return (valid[b[param] - 1][a[param] - 1][pt] != 0);
    }

    // --------------------------------------------------------------------------------------------------
    // Write accessors
    // --------------------------------------------------------------------------------------------------
    virtual void set_RI(S32 pt, S32 param, COMPLEX_DOUBLE val)
    {
        const S32 B[4] = { 1, 2, 1, 2 };
        const S32 A[4] = { 1, 1, 2, 2 };

        S32 a = A[param] - 1;
        S32 b = B[param] - 1;

        RI[b][a][pt] = val;
        valid[b][a][pt] = SNPTYPE::RI;
    }

    virtual void set_RI(S32 pt, S32 b, S32 a, COMPLEX_DOUBLE val)
    {
        RI[b][a][pt] = val;
        valid[b][a][pt] = SNPTYPE::RI;
    }

    // --------------------------------------------------------------------------------------------------
    // Accessor routines return valid S-parameter value in desired format
    //
    // Parameters can be requested by index [b,a] or by position in Touchstone file 0-3
    // (0=S11, 1=S21, 2=S12, 3=S22)
    //
    // Attempting to request a value that has never been written in any format
    // triggers an assert
    // --------------------------------------------------------------------------------------------------
    virtual SPARAM::RI get_RI(S32 pt, S32 param)
    {
        const S32 b[4] = { 1, 2, 1, 2 };
        const S32 a[4] = { 1, 1, 2, 2 };

        assert(param < 4);
        return get_RI(pt, b[param] - 1, a[param] - 1);
    }

    virtual void get_RI(S32 pt)
    {
        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                get_RI(pt, b, a);
            }
        }
    }

    virtual SPARAM::RI get_RI(S32 pt, S32 b, S32 a)
    {
        if (valid[b][a][pt] & SNPTYPE::RI)
        {
            return RI[b][a][pt];
        }

        if (valid[b][a][pt] & SNPTYPE::MA)
        {
            RI[b][a][pt] = MA[b][a][pt];
        }
        else if (valid[b][a][pt] & SNPTYPE::DB)
        {
            RI[b][a][pt] = DB[b][a][pt];
        }
        else assert(0);

        valid[b][a][pt] |= SNPTYPE::RI;
        return RI[b][a][pt];
    }

    virtual SPARAM::MA get_MA(S32 pt, S32 param)
    {
        const S32 b[4] = { 1, 2, 1, 2 };
        const S32 a[4] = { 1, 1, 2, 2 };

        assert(param < 4);
        return get_MA(pt, b[param] - 1, a[param] - 1);
    }

    virtual void get_MA(S32 pt)
    {
        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                get_MA(pt, b, a);
            }
        }
    }

    virtual SPARAM::MA get_MA(S32 pt, S32 b, S32 a)
    {
        if (valid[b][a][pt] & SNPTYPE::MA)
        {
            return MA[b][a][pt];
        }

        if (valid[b][a][pt] & SNPTYPE::DB)
        {
            MA[b][a][pt] = DB[b][a][pt];
        }
        else if (valid[b][a][pt] & SNPTYPE::RI)
        {
            MA[b][a][pt] = RI[b][a][pt];
        }
        else assert(0);

        valid[b][a][pt] |= SNPTYPE::MA;
        return MA[b][a][pt];
    }

    virtual void get_DB(S32 pt)
    {
        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                get_DB(pt, b, a);
            }
        }
    }

    virtual SPARAM::DB get_DB(S32 pt, S32 param)
    {
        const S32 b[4] = { 1, 2, 1, 2 };
        const S32 a[4] = { 1, 1, 2, 2 };

        assert(param < 4);
        return get_DB(pt, b[param] - 1, a[param] - 1);
    }

    virtual SPARAM::DB get_DB(S32 pt, S32 b, S32 a)
    {
        if (valid[b][a][pt] & SNPTYPE::DB)
        {
            return DB[b][a][pt];
        }

        if (valid[b][a][pt] & SNPTYPE::MA)
        {
            DB[b][a][pt] = MA[b][a][pt];
        }
        else if (valid[b][a][pt] & SNPTYPE::RI)
        {
            DB[b][a][pt] = RI[b][a][pt];
        }
        else assert(0);

        valid[b][a][pt] |= SNPTYPE::DB;
        return DB[b][a][pt];
    }

    virtual SPARAM::CZ get_CZ(S32 pt, S32 param)
    {
        const S32 b[4] = { 1, 2, 1, 2 };
        const S32 a[4] = { 1, 1, 2, 2 };

        assert(param < 4);
        return get_CZ(pt, b[param] - 1, a[param] - 1);
    }

    virtual void get_CZ(S32 pt)
    {
        for (S32 b = 0; b < n_ports; b++)
        {
            for (S32 a = 0; a < n_ports; a++)
            {
                get_CZ(pt, b, a);
            }
        }
    }

    virtual SPARAM::CZ get_CZ(S32 pt, S32 b, S32 a)
    {
        if (valid[b][a][pt] & SNPTYPE::CZ)
        {
            return CZ[b][a][pt];
        }

        CZ[b][a][pt] = SPARAM::CZ(get_MA(pt, b, a), Zo.real);

        valid[b][a][pt] |= SNPTYPE::CZ;
        return CZ[b][a][pt];
    }

    // --------------------------------------------------------------------------------------------------
    // Frequency-based queries return S-parameter value in desired format, interpolated
    // to specified frequency
    //
    //   RI: Interpolate cartesian I and Q independently
    //   MA: Interpolate cartesian magnitude and polar angle, returning phase in [-PI,PI]
    //   DB: Same as MA with interpolated magnitude converted back to DB
    //   CZ: Same as MA with interpolated value converted back to CZ
    //
    // Phase/magnitude values beyond min/max frequencies are extrapolated as specified by flags:
    //
    //   EXT_ZERO: Return valid zero value for all out-of-range queries
    //   EXT_LEND: Return valid copy of leftmost endpoint for queries below min freq
    //   EXT_REND: Return valid copy of rightmost endpoint for queries above max freq
    // --------------------------------------------------------------------------------------------------
    virtual SPARAM::RI get_RI(DOUBLE Hz, S32 b, S32 a, U8 flags, bool *in_range)
    {
        if (in_range != NULL)
        {
            *in_range = TRUE;
        }

        if (Hz < min_Hz)
        {
            if (flags & SPARAM::EXT_ZERO)
            {
                return SPARAM::RI(0.0, 0.0);
            }
            else if (flags & SPARAM::EXT_LEND)
            {
                return get_RI(0, b, a);
            }
            else
            {
                if (in_range != NULL)
                {
                    *in_range = FALSE;
                }

                return SPARAM::RI(0.0, 0.0);
            }
        }

        if (Hz > max_Hz)
        {
            if (flags & SPARAM::EXT_ZERO)
            {
                return SPARAM::RI(0.0, 0.0);
            }
            else if (flags & SPARAM::EXT_REND)
            {
                return get_RI(n_points - 1, b, a);
            }
            else
            {
                if (in_range != NULL)
                {
                    *in_range = FALSE;
                }
                return SPARAM::RI(0.0, 0.0);
            }
        }

        DOUBLE A = 0.0;
        S32 p0 = nearest_freq_Hz(Hz, &A);

        if (p0 >= n_points - 1)
        {
                return get_RI(n_points - 1, b, a);
        }

        S32 p1 = p0 + 1;

        SPARAM::RI v0 = get_RI(p0, b, a);
        SPARAM::RI v1 = get_RI(p1, b, a);

        return SPARAM::RI(v0.real + ((v1.real - v0.real) * A),
                v0.imag + ((v1.imag - v0.imag) * A));
    }

    virtual SPARAM::MA get_MA(DOUBLE Hz, S32 b, S32 a, U8 flags, bool *in_range)
    {
        if (in_range != NULL)
        {
            *in_range = TRUE;
        }

        if (Hz < min_Hz)
        {
            if (flags & SPARAM::EXT_ZERO)
            {
                    return SPARAM::MA(0.0, 0.0);
            }
            else if (flags & SPARAM::EXT_LEND)
            {
                return get_MA(0, b, a);
            }
            else
            {
                if (in_range != NULL)
                {
                        *in_range = FALSE;
                }

                return SPARAM::MA(0.0, 0.0);
            }
        }

        if (Hz > max_Hz)
        {
                if (flags & SPARAM::EXT_ZERO)
                {
                        return SPARAM::MA(0.0, 0.0);
                }
                else if (flags & SPARAM::EXT_REND)
                {
                        return get_MA(n_points - 1, b, a);
                }
                else
                {
                        if (in_range != NULL)
                        {
                                *in_range = FALSE;
                        }

                        return SPARAM::MA(0.0, 0.0);
                }
        }

        DOUBLE A = 0.0;
        S32 p0 = nearest_freq_Hz(Hz, &A);

        if (p0 >= n_points - 1)
        {
                return get_MA(n_points - 1, b, a);
        }

        S32 p1 = p0 + 1;

        SPARAM::MA v0 = get_MA(p0, b, a);
        SPARAM::MA v1 = get_MA(p1, b, a);

        DOUBLE d = v1.deg - v0.deg;

        while (d < -180.0) d += 360.0;      // This keeps a wrap from (e.g.) 175 to -175 from incorrectly returning an interpolated point near 0
        while (d > 180.0) d -= 360.0;      // (which happens anyway when the data is plotted, but for the sake of correctness we'll do it here)

        DOUBLE interp_mag = v0.mag + ((v1.mag - v0.mag) * A);
        DOUBLE interp_deg = v0.deg + (d * A);

        while (interp_deg < -180.0) interp_deg += 360.0;
        while (interp_deg > 180.0) interp_deg -= 360.0;

        return SPARAM::MA(interp_mag,
                interp_deg);
    }

    virtual SPARAM::DB get_DB(DOUBLE Hz, S32 b, S32 a, U8 flags, bool *in_range)
    {
        return SPARAM::DB(get_MA(Hz, b, a, flags, in_range));
    }

    virtual SPARAM::CZ get_CZ(DOUBLE Hz, S32 b, S32 a, U8 flags, bool *in_range)
    {
        return SPARAM::CZ(get_MA(Hz, b, a, flags, in_range), Zo.real);
    }

    // --------------------------------------------------------------------------------------------------
    // Perform T-Check calibration assessment
    // --------------------------------------------------------------------------------------------------
    virtual bool T_check(DOUBLE *out)
    {
        #ifdef _MSC_VER
            for (S32 pt = 0; pt < n_points; pt++)
            {
                if (freq_Hz[pt] == 0.0)       // skip DC bin, if any
                {
                    out[pt] = 0.0;
                    continue;
                }

                get_RI(pt);

                DOUBLE b11 = RI[0][0][pt].cabs();
                DOUBLE b12 = RI[0][1][pt].cabs();
                DOUBLE c11 = RI[1][0][pt].cabs();
                DOUBLE c12 = RI[1][1][pt].cabs();

                DOUBLE den = COMPLEX_DOUBLE::csqrt((1.0 - b11*b11 - b12*b12) * (1.0 - c11*c11 - c12*c12)).cabs();

                if (fabs(den) < 1E-30)
                {
                    message_printf(SPARAM::MSG_ERROR, (C8*)"T-Check formula underflow at point %d (%lf MHz)", pt, freq_Hz[pt] / 1E6);
                    return FALSE;
                }

                COMPLEX_DOUBLE num = ((RI[0][0][pt] * (RI[1][0][pt].conj())) +
                        (RI[0][1][pt] * (RI[1][1][pt].conj())));

                out[pt] = ((num.cabs() / den) - 1.0) * 100.0;
            }

            return TRUE;
        #else
            message_printf(SPARAM::MSG_ERROR, (C8*)"T-Check supported only when built with Visual Studio");
            return FALSE;
        #endif
    }

    // --------------------------------------------------------------------------------------------------
    // Save data to Touchstone 1.1 file
    // --------------------------------------------------------------------------------------------------
    /* Parameters
        const C8 *filename // Output filename
        const C8 *data_format = SPARAM::DEF_DATA_FORMAT // e.g., "MA",
        const C8 *freq_format = SPARAM::DEF_FREQ_FORMAT // e.g., "GHZ",
        const C8 *header = NULL	// optional
        const C8 *single_param_type = NULL // optional
    */
    virtual bool write_SNP_file(const C8 *filename,
                                const C8 *data_format = SPARAM::DEF_DATA_FORMAT,
                                const C8 *freq_format = SPARAM::DEF_FREQ_FORMAT,
                                const C8 *header = NULL,
                                const C8 *single_param_type = NULL)
    {
        if (n_ports > 2)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)">2 ports not supported");
            return FALSE;
        }

        FILE *out = fopen(filename, "wt");

        if (out == NULL)
        {
            message_printf(SPARAM::MSG_ERROR, (C8*)"Couldn't open %s", filename);
            return FALSE;
        }

        if (header != NULL)
        {
            fprintf(out, "%s", sanitize(header));

            C8 term = header[strlen(header) - 1];
            if ((term != 10) && (term != 13))
            {
                fprintf(out, "\n");
            }
        }

        if (n_ports == 1)
            fprintf(out, "! Params: %s\n", (single_param_type == NULL) ? "S11" : single_param_type);
        else
            fprintf(out, "! Params: S11 S21 S12 S22\n");

        if ((min_Hz == DBL_MAX) || (max_Hz == -DBL_MAX))
        {
            fprintf(out, "! Points = %d\n", n_points);
        }
        else
        {
            fprintf(out, "! Start frequency: %0.9lf GHz\n! Stop frequency:  %0.9lf GHz\n! Points: %d\n",
                    (min_Hz/1000000000.0),
                    (max_Hz/1000000000.0),
                    n_points);
        }

        fprintf(out, "!\n");

        const C8    *freq_txt[] = { "HZ", "KHZ", "MHZ", "GHZ" };
        const DOUBLE freq_fac[] = { 1E0, 1E3, 1E6, 1E9 };

        S32 freq_fmt = 3;

        for (S32 i = 0; i < 3; i++)
        {
            if (!_stricmp(freq_format, freq_txt[i]))
            {
                freq_fmt = i;
                break;
            }
        }

        U8 format = SNPTYPE::MA;

        if (!_stricmp(data_format, "DB")) format = SNPTYPE::DB;
        else if (!_stricmp(data_format, "RI")) format = SNPTYPE::RI;

        switch (format)
        {
            case SNPTYPE::MA: fprintf(out, "# %s S MA R %lG\n", freq_txt[freq_fmt], Zo.real); break;
            case SNPTYPE::DB: fprintf(out, "# %s S DB R %lG\n", freq_txt[freq_fmt], Zo.real); break;
            case SNPTYPE::RI: fprintf(out, "# %s S RI R %lG\n", freq_txt[freq_fmt], Zo.real); break;
            default: assert(0);
        }

        for (S32 i = 0; i < n_points; i++)
        {
            fprintf(out, "%lf ", freq_Hz[i] / freq_fac[freq_fmt]);

            for (S32 a = 0; a < n_ports; a++)
            {
                for (S32 b = 0; b < n_ports; b++)
                {
                    switch (format)
                    {
                        case SNPTYPE::MA:
                        {
                            SPARAM::MA val = get_MA(i, b, a);

                            fprintf(out, "%lf %lf ", val.mag, val.deg);
                            break;
                        }

                        case SNPTYPE::DB:
                        {
                            SPARAM::DB val = get_DB(i, b, a);

                            fprintf(out, "%lf %lf ", val.dB, val.deg);
                            break;
                        }

                        case SNPTYPE::RI:
                        {
                            SPARAM::RI val = get_RI(i, b, a);

                            fprintf(out, "%lf %lf ", val.real, val.imag);
                            break;
                        }

                        default:
                                assert(0);
                    }
                }
            }
            fprintf(out, "\n");
        }

        fclose(out);

        return TRUE;
    }

    // --------------------------------------------------------------------------------------------------
    // Load contents of Touchstone 1.1 file (e.g., .s1p, .s2p)
    //
    // NB: There's no straightforward way to tell how many ports are specified in a
    // Touchstone 1.X file, so the target database size must be specified in file_ports
    // --------------------------------------------------------------------------------------------------
    virtual bool read_SNP_file(const C8 *filename, S32 file_ports)
    {
        //
        // Pass 1: Discard any existing data, then count # of points defined in file and allocate arrays
        // Pass 2: Read data from the file
        //
        clear();
        init();

        DOUBLE file_scale = 1E9;            // Defaults from Touchstone 1.1 spec
        C8     file_param = 'S';
        U8     file_format = SNPTYPE::MA;
        DOUBLE file_R = 50.0;
        S32    file_points = 0;

        S32 pt = 0;

        for (S32 pass = 1; pass <= 2; pass++)
        {
            FILE *in = fopen(filename, "rt");

            if (in == NULL)
            {
                message_printf(SPARAM::MSG_ERROR, (C8*)"Couldn't open %s", filename);
                return FALSE;
            }

            if (pass == 2)
            {
                message_printf(SPARAM::MSG_VERBOSE, (C8*)"  Points: %d\n", file_points);

                if (!alloc(file_ports, file_points))
                {
                    return FALSE;
                }
            }

            for (;;)
            {
                C8 linbuf[2048] = { 0 };

                C8 *result = fgets(linbuf,
                        sizeof(linbuf) - 1,
                        in);

                if (result == NULL)
                {
                    break;
                }

                //
                // Remove leading and trailing spaces as well as text on lines
                // following '!' comments, and skip any blank lines
                //

                C8 *txt = linbuf;
                C8 *end = linbuf;
                bool leading = TRUE;

                S32 l = strlen(linbuf);

                for (S32 i = 0; i < l; i++)
                {
                    if (linbuf[i] == '!')
                    {
                        break;
                    }

                    if (!isspace((U8)linbuf[i]))
                    {
                        if (leading)
                        {
                            txt = &linbuf[i];
                            leading = FALSE;
                        }
                        end = &linbuf[i];
                    }
                }
                end[1] = 0;

                if (leading || (!strlen(txt)))
                {
                    continue;
                }

                //
                // Only V1.x format supported for now, so flag any 2.X/IBIS-style options
                //
                if (txt[0] == '[')
                {
                    message_printf(SPARAM::MSG_ERROR, (C8*)"Touchstone 2.0 and later files not supported");
                    return FALSE;
                }

                //
                // Parse option line (e.g., # GHZ S MA R 50)
                //
                if (txt[0] == '#')
                {
                    if (file_points == 0)      // must be the first non-comment line in the file
                    {
                        _strupr(txt);
                        C8 *src = &txt[1];

                        while (*src)
                        {
                            if (!_strnicmp(src, "GHZ", 3)) { file_scale = 1E9; src += 3; continue; }
                            if (!_strnicmp(src, "MHZ", 3)) { file_scale = 1E6; src += 3; continue; }
                            if (!_strnicmp(src, "KHZ", 3)) { file_scale = 1E3; src += 3; continue; }
                            if (!_strnicmp(src, "HZ", 2))  { file_scale = 1E0; src += 2; continue; }

                            if (!_strnicmp(src, "DB", 2))  { file_format = SNPTYPE::DB; src += 2; continue; }
                            if (!_strnicmp(src, "MA", 2))  { file_format = SNPTYPE::MA; src += 2; continue; }
                            if (!_strnicmp(src, "RI", 2))  { file_format = SNPTYPE::RI; src += 2; continue; }

                            if (!_strnicmp(src, "S", 1))   { file_param = 'S'; src += 1; continue; } // Scattering parameters
                            if (!_strnicmp(src, "Y", 1))   { file_param = 'Y'; src += 1; continue; } // Admittance parameters
                            if (!_strnicmp(src, "Z", 1))   { file_param = 'Z'; src += 1; continue; } // Impedance parameters
                            if (!_strnicmp(src, "H", 1))   { file_param = 'H'; src += 1; continue; } // Hybrid-h parameters
                            if (!_strnicmp(src, "G", 1))   { file_param = 'G'; src += 1; continue; } // Hybrid-g parameters

                            if (!_strnicmp(src, "R ", 2))
                            {
                                S32 len = 0;
                                sscanf(src, "R %lf%n", &file_R, &len);
                                src += len;
                                continue;
                            }

                            if (!isspace((U8)*src))
                            {
                                message_printf(SPARAM::MSG_WARNING, (C8*)"Unknown option '%s' in %s\n", src, filename);
                            }

                            src++;
                        }

                        message_printf(SPARAM::MSG_VERBOSE, (C8*)"\nFilename: %s\n  Header: %s\n   Scale: %lf\n   Param: %c\n    Type: 0x%.2X\n       R: %lf\n",
                                filename, txt, file_scale, file_param, file_format, file_R);

                        if (file_param != 'S')
                        {
                            message_printf(SPARAM::MSG_ERROR, (C8*)"%c-parameter files not supported", file_param);
                            return FALSE;
                        }
                    }
                    continue;
                }

                if (pass == 1)
                {
                    ++file_points;
                    continue;
                }

                //
                // Store frequency and complex port data for each point in file
                //
                // Frequency must increase monotonically -- if it doesn't, we'll assume we've
                // hit a noise-parameter block and truncate the record accordingly
                //
                DOUBLE R[2][2] = { 0 };
                DOUBLE I[2][2] = { 0 };

                if (n_ports == 1)
                {
                    sscanf(txt, "%lf %lf %lf",
                            &freq_Hz[pt],
                            &R[0][0], &I[0][0]); // S11
                }
                else
                {
                    sscanf(txt, "%lf %lf %lf %lf %lf %lf %lf %lf %lf",
                            &freq_Hz[pt],
                            &R[0][0], &I[0][0],  // S11
                            &R[1][0], &I[1][0],  // S21
                            &R[0][1], &I[0][1],  // S12
                            &R[1][1], &I[1][1]); // S22
                }

                freq_Hz[pt] *= file_scale;

                if (freq_Hz[pt] < max_Hz)
                {
                    message_printf(SPARAM::MSG_VERBOSE, (C8*)"  Notice: Truncating file to %d points due to presence of noise record\n", pt);
                    file_points = n_points = pt;
                    break;
                }

                if (freq_Hz[pt] < min_Hz) min_Hz = freq_Hz[pt];
                if (freq_Hz[pt] > max_Hz) max_Hz = freq_Hz[pt];

                for (S32 b = 0; b < n_ports; b++)
                {
                    for (S32 a = 0; a < n_ports; a++)
                    {
                        valid[b][a][pt] = file_format;

                        switch (file_format)
                        {
                            case SNPTYPE::DB:
                            {
                                DB[b][a][pt].dB = R[b][a];
                                DB[b][a][pt].deg = I[b][a];
                                break;
                            }

                            case SNPTYPE::MA:
                            {
                                MA[b][a][pt].mag = R[b][a];
                                MA[b][a][pt].deg = I[b][a];
                                break;
                            }

                            case SNPTYPE::RI:
                            {
                                RI[b][a][pt].real = R[b][a];
                                RI[b][a][pt].imag = I[b][a];
                                break;
                            }
                        }
                    }
                }

                pt++;
            } //  end for (;;)
            fclose(in);
            in = NULL;
        } // for (S32 pass = 1; pass <= 2; pass++)

        message_printf(SPARAM::MSG_VERBOSE, (C8*)"  Min Hz: %lf\n  Max Hz: %lf\n", min_Hz, max_Hz);
        message_printf(SPARAM::MSG_VERBOSE, (C8*)"\n");

        Zo = file_R;
        return TRUE;
    }

    // --------------------------------------------------------------------------------------------------
    // Utility functions to generate interpolated frequency array compatible with
    // the spline and linear interpolators below
    //
    // Optionally return the first and last valid SNP indexes
    // --------------------------------------------------------------------------------------------------

    virtual void interp_Hz(DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            S32     n_out_points,
            DOUBLE *out_Hz = NULL,
            S32    *p0 = NULL,
            S32    *p1 = NULL)
    {
            DOUBLE Hz = out_min_Hz;
            DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

            for (S32 i = 0; i < n_out_points; i++)
            {
                    if (out_Hz != NULL)
                            out_Hz[i] = Hz;

                    if ((p0 != NULL) && (Hz >= min_Hz) && (*p0 == -1))
                            *p0 = i;

                    if ((p1 != NULL) && (Hz <= max_Hz))
                            *p1 = i;

                    Hz += d_Hz;
            }
    }

    static void interp_Hz(DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            DOUBLE *out_Hz,
            S32     n_out_points)
    {
        DOUBLE Hz = out_min_Hz;
        DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

        for (S32 i = 0; i < n_out_points; i++)
        {
            out_Hz[i] = Hz;
            Hz += d_Hz;
        }
    }

    // ---------------------------------
    // Spline interpolators
    // ---------------------------------

    virtual void spline_dB(S32     b,
            S32     a,
            DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            S32     n_out_points,
            DOUBLE *out_dB,
            DOUBLE *out_Hz = NULL)
    {
        Q_UNUSED(b);
        Q_UNUSED(a);
        DOUBLE *src_X = (DOUBLE *)alloca(n_points * sizeof(DOUBLE));
        DOUBLE *src_Y = (DOUBLE *)alloca(n_points * sizeof(DOUBLE));

        for (S32 i = 0; i < n_points; i++)
        {
            src_X[i] = freq_Hz[i];
            src_Y[i] = get_MA(i, 1, 0).mag;
        }

        DOUBLE *dest_X = (DOUBLE *)alloca(n_out_points * sizeof(DOUBLE));
        DOUBLE *dest_Y = (DOUBLE *)alloca(n_out_points * sizeof(DOUBLE));

        S32 p0 = -1;
        S32 p1 = -1;

        DOUBLE Hz = out_min_Hz;
        DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

        for (S32 i = 0; i < n_out_points; i++)         // Find first and last screen points that have valid S2P data
        {
            dest_X[i] = Hz;
            dest_Y[i] = 1E-15;

            if ((Hz >= min_Hz) && (p0 == -1))
                p0 = i;

            if (Hz <= max_Hz)
                p1 = i;

            Hz += d_Hz;
        }

        if ((p0 != -1) && (p1 != -1))
        {
            S32 dN = p1 - p0 + 1;

            if (dN > 0)
            {
                DOUBLE *dX = &dest_X[p0];              // Interpolate S2P data to uniform grid between frequencies of interest
                DOUBLE *dY = &dest_Y[p0];

                spline_gen(src_X, src_Y, n_points,
                        dX, dY, dN);
            }
        }

        if (p0 != -1)                                // Set all other points equal to first/last valid values
        {
            for (S32 i = 0; i < p0; i++)
                dest_Y[i] = dest_Y[p0];
        }

        if (p1 != -1)
        {
            for (S32 i = p1 + 1; i < n_out_points; i++)
                dest_Y[i] = dest_Y[p1];
        }

        if (out_Hz == NULL)                          // Return interpolated linear magnitude in dB
        {
            for (S32 i = 0; i < n_out_points; i++)      // Clamp the log10() argument since steep edges can cause ringing into the negative range
            {
                out_dB[i] = 20.0 * log10(max(1E-15, dest_Y[i]));
            }
        }
        else
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_Hz[i] = dest_X[i];
                out_dB[i] = 20.0 * log10(max(1E-15, dest_Y[i]));
            }
        }
    }

    virtual void spline_deg(S32     b,
            S32     a,
            DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            S32     n_out_points,
            DOUBLE *out_deg,
            DOUBLE *out_Hz = NULL)
    {
        DOUBLE *src_X = (DOUBLE *)alloca(n_points * sizeof(DOUBLE));
        DOUBLE *src_Y = (DOUBLE *)alloca(n_points * sizeof(DOUBLE));

        for (S32 i = 0; i < n_points; i++)
        {
            src_X[i] = freq_Hz[i];
            src_Y[i] = get_MA(i, b, a).deg;
        }

        DOUBLE *dest_X = (DOUBLE *)alloca(n_out_points * sizeof(DOUBLE));
        DOUBLE *dest_Y = (DOUBLE *)alloca(n_out_points * sizeof(DOUBLE));

        DOUBLE Hz = out_min_Hz;
        DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

        S32 p0 = -1;
        S32 p1 = -1;

        for (S32 i = 0; i < n_out_points; i++)         // Find first and last screen points that have valid S2P data
        {
            dest_X[i] = Hz;
            dest_Y[i] = 180.0;

            if ((Hz >= min_Hz) && (p0 == -1))
                    p0 = i;

            if (Hz <= max_Hz)
                    p1 = i;

            Hz += d_Hz;
        }

        if ((p0 != -1) && (p1 != -1))
        {
            S32 dN = p1 - p0 + 1;

            if (dN > 0)
            {
                DOUBLE *dX = &dest_X[p0];              // Interpolate S2P data to uniform grid between frequencies of interest
                DOUBLE *dY = &dest_Y[p0];

                spline_gen(src_X, src_Y, n_points,
                        dX, dY, dN);
            }
        }

        if (p0 != -1)                                // Set all other points equal to first/last valid values
        {
            for (S32 i = 0; i < p0; i++)
                dest_Y[i] = dest_Y[p0];
        }

        if (p1 != -1)
        {
            for (S32 i = p1 + 1; i < n_out_points; i++)
                dest_Y[i] = dest_Y[p1];
        }

        if (out_Hz == NULL)
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_deg[i] = dest_Y[i];
            }
        }
        else
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_Hz[i] = dest_X[i];
                out_deg[i] = dest_Y[i];
            }
        }
    }

    // ---------------------------------
    // Linear interpolators
    // ---------------------------------
    virtual void lerp_dB(S32     b,
            S32     a,
            DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            S32     n_out_points,
            DOUBLE *out_dB,
            DOUBLE *out_Hz,
            bool   *out_valid,
            U8      flags)
    {
        DOUBLE Hz = out_min_Hz;
        DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

        if (out_Hz == NULL)
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_dB[i] = get_DB(Hz, b, a, flags, &out_valid[i]).dB;
                Hz += d_Hz;
            }
        }
        else
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_Hz[i] = Hz;
                out_dB[i] = get_DB(Hz, b, a, flags, &out_valid[i]).dB;
                Hz += d_Hz;
            }
        }
    }

    virtual void lerp_deg(S32     b,
            S32     a,
            DOUBLE  out_min_Hz,
            DOUBLE  out_max_Hz,
            S32     n_out_points,
            DOUBLE *out_deg,
            DOUBLE *out_Hz,
            bool   *out_valid,
            U8      flags)
    {
        DOUBLE Hz = out_min_Hz;
        DOUBLE d_Hz = (out_max_Hz - out_min_Hz) / n_out_points;

        if (out_Hz == NULL)
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_deg[i] = get_DB(Hz, b, a, flags, &out_valid[i]).deg;
                Hz += d_Hz;
            }
        }
        else
        {
            for (S32 i = 0; i < n_out_points; i++)
            {
                out_Hz[i] = Hz;
                out_deg[i] = get_DB(Hz, b, a, flags, &out_valid[i]).deg;
                Hz += d_Hz;
            }
        }
    }
};

