#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QThread>
#include <QFileDialog>
#include <QDir>

#include <QFontDatabase>
#include <QSettings>
#include <QDesktopServices>
#include <QElapsedTimer>

#include "version.h"

#include "progress.h"

#include "spline.cpp"
#include "sparams.cpp"

#include <cstdio>

// NI VISA API Info
// http://zone.ni.com/reference/en-XX/help/370131S-01/ni-visa/examplevisamessage-basedapplication/

void conv_form1_real_imag(t_form1_raw_imag_real *data_in, double* real, double* imag)
{
    short real_raw;
    short imag_raw;
    double pow_2_exp;

    real_raw = (((unsigned short)data_in->real_msb) << (unsigned short)8) + (unsigned short)data_in->real_lsb;
    imag_raw = (((unsigned short)data_in->imag_msb) << (unsigned short)8) + (unsigned short)data_in->imag_lsb;
    pow_2_exp = pow_2_exp_tab[data_in->common_exp];

    *real = ( (double)(real_raw) / (double)(1<<15) ) * pow_2_exp;
    *imag = ( (double)(imag_raw) / (double)(1<<15) ) * pow_2_exp;
}

// ViSession instr =>Visa Session
bool MainWindow::instrument_setup(ViSession instr)
{
    qDebug("instrument_setup start");
    #define DATA_SIZE (512)
    ViStatus stat;
    ViByte data[DATA_SIZE+1] = { 0 };
    ViUInt32 retCount;
    double if_bandwidth;
    double out_power_level;

    if (debug_mode)
    {
        viPrintf(instr, (ViString)"DEBUON;\n");
    }

    // Outputs the identification string for the analyzer (like IDN?)
    viPrintf(instr, (ViString)"OUTPIDEN\n");
    memset(data, 0, DATA_SIZE);
    stat = viRead(instr, data, DATA_SIZE, &retCount);
    qDebug("viRead() data=\"%s\" retCount=%d stat=%d", data, retCount, stat);
    if(stat != 0)
    {
        qDebug("Error to communicate with GPIB stat=%d", stat);
        this->ui->plainTextEdit->appendPlainText("Error to communicate with GPIB");
        return FALSE;
    }
    _snprintf(instrument_name, sizeof(instrument_name) - 1, "%s", data);
    {
        C8 *d = &instrument_name[strlen(instrument_name) - 1];
        while ((d >= instrument_name) && ((*d == 10) || (*d == 13)))
        {
            *d = 0;
        }
    }
    qDebug("instrument_name=\"%s\"", instrument_name);
    this->ui->plainTextEdit->appendPlainText((char*)instrument_name);

    // Read Instrument Options ASCII
    viPrintf(instr, (ViString)"OUTPOPTS\n");
    memset(instrument_opts, 0, sizeof(instrument_opts));
    stat = viRead(instr, (ViByte*)instrument_opts, (sizeof(instrument_opts)-1), &retCount);
    {
        C8 *d = &instrument_opts[strlen(instrument_opts) - 1];
        while ((d >= instrument_opts) && ((*d == 10) || (*d == 13)))
        {
            *d = 0;
        }
    }
    qDebug(" end param OUTPOPTS viRead() result=\"%s\" retCount=%d stat=%d time=%lld ms", instrument_opts, retCount, stat);
    qDebug("instrument_opts=\"%s\"", instrument_opts);

    // Read IF bandwidth in Hz
    viPrintf(instr, (ViString)"IFBW?\n");
    if_bandwidth = 0.0;
    stat = viScanf(instr,(ViString)"%lf", &if_bandwidth);
    sprintf(instrument_if_bandwidth, "IF bandwidth: %.lf Hz", if_bandwidth);
    qDebug("instrument_if_bandwidth=\"%s\"", instrument_if_bandwidth);

    // Check Smoothing ON or OFF
    viPrintf(instr, (ViString)"SMOOO?;\n");
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug(" end param SMOOO? viRead() result=\"%c\" retCount=%d stat=%d time=%lld ms", data[0], retCount, stat);
    if(data[0] == '1')
    {
        sprintf(instrument_smoothing, "Smoothing ON");
    }else
    {
        sprintf(instrument_smoothing, "Smoothing OFF");
    }
    qDebug("instrument_smoothing=\"%s\"", instrument_smoothing);

    // Check Averaging ON or OFF
    viPrintf(instr, (ViString)"AVERO?;\n");
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug(" end param AVERO? viRead() result=\"%c\" retCount=%d stat=%d time=%lld ms", data[0], retCount, stat);
    if(data[0] == '1')
    {
        sprintf(instrument_averaging, "Averaging ON");
    }else
    {
        sprintf(instrument_averaging, "Averaging OFF");
    }
    qDebug("instrument_averaging=\"%s\"", instrument_averaging);

    // Check Correction ON or OFF
    viPrintf(instr, (ViString)"CORR?;\n");
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug(" end param CORR? viRead() result=\"%c\" retCount=%d stat=%d time=%lld ms", data[0], retCount, stat);
    if(data[0] == '1')
    {
        sprintf(instrument_correction, "Correction ON");
    }else
    {
        sprintf(instrument_correction, "Correction OFF");
    }
    qDebug("instrument_correction=\"%s\"", instrument_correction);

    // Read Output power level in dBm
    viPrintf(instr, (ViString)"POWE?;\n");
    out_power_level = 0.0;
    stat = viScanf(instr,(ViString)"%lf", &out_power_level);
    sprintf(instrument_out_power_level, "Output power level: %.6lf dBm", out_power_level);
    qDebug("instrument_out_power_level=\"%s\"", instrument_out_power_level);

    viPrintf(instr, (ViString)"HOLD;\n");
    // Wait for the analyzer to finish
    viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", data[0], retCount, stat);

    qDebug("instrument_setup end");
    return TRUE;
}

/*
Parameters:
ViSession instr =>Visa Session
C8             *param => "S11" or "S21" or "S12" or "S22"
C8             *query => "OUTPDATA" (Default) or "OUTPFORM"
COMPLEX_DOUBLE *dest 	=> dest data
S32             cnt		=> number of points (n_AC_points)
S32             progress_fraction => Progression in %
*/
bool MainWindow::read_complex_trace_FORM4(QProgressDialog *progress,
                                    ViSession instr,
                                    C8             *param,
                                    C8             *query,
                                    COMPLEX_DOUBLE *dest,
                                    S32             cnt,
                                    S32             progress_fraction)
{
    ViByte buf[3] = { 0 };
    ViUInt32 retCount;
    ViStatus stat;
    U8 mask = 0x40;
    QElapsedTimer timer;

    qDebug(" read_complex_trace_FORM4() start param=%s query=%s", param, query);
    timer.start();

    viPrintf(instr, (ViString)"CLES;SRE 4;ESNB 1;\n");
    qDebug(" viPrintf(\"CLES;SRE 4;ESNB 1;\")");
    mask = 0x40;// Extended register bit 0 = SING sweep complete; map it to status bit and enable SRQ on it

    viPrintf(instr, (ViString)"%s;FORM4;OPC?;SING;\n", param);
    qDebug(" viPrintf(\"%s;FORM4;OPC?;SING;\") time=%lld ms", param, timer.elapsed());
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug(" end param viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d time=%lld ms", buf[0], retCount, stat, timer.elapsed());

    viPrintf(instr, (ViString)"CLES;SRE 0;\n");
    viPrintf(instr, (ViString)"%s;\n", query);

    qDebug(" loop start 0 to %d", cnt);
    timer.start();
    for (S32 i = 0; i < cnt; i++)
    {
        DOUBLE I = DBL_MIN;
        DOUBLE Q = DBL_MIN;

        stat = viScanf(instr,(ViString)"%lf, %lf",&I, &Q);
        if(stat != 0)
        {
            qDebug(" i=%d viScanf() I=%lf Q=%lf stat=%d", i, I, Q, stat);
        }

        if ((I == DBL_MIN) || (Q == DBL_MIN))
        {
            qDebug(" Error VNA read timed out reading %s (point %d of %d points)", param, i, cnt);
            return FALSE;
        }

        dest[i].real = I;
        dest[i].imag = Q;

        //qDebug("Progress %d%%\n", ((i * 20) / cnt) + progress_fraction);
        progress->setValue(((i * 20) / cnt) + progress_fraction);
        QApplication::processEvents(); // Force refresh process all events
    }

    qDebug(" read_complex_trace_FORM4() loop end time=%lld ms", timer.elapsed());

    return TRUE;
}

/*
save_SnP_FORM4
Parameters:
ViSession instr =>Visa Session
S32 SnP => 1 = S1P or 2 = S2P
C8 *param => "" for S2P, "S11", "S21" or "S22" for S1P
C8 *query => "OUTPDATA" (Default) or "OUTPFORM"
DOUBLE R_ohms => 50.0
const C8 *data_format => S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
const C8 *freq_format => "Hz"(Default), "kHz", "MHz", "GHz"
S32 DC_entry => 0 = None(Default)
const C8 *explicit_filename => Output filename
*/
bool MainWindow::save_SnP_FORM4(QProgressDialog* progress,
                            ViSession instr,
                            S32       SnP,
                            C8       *param,
                            C8       *query,
                            DOUBLE    R_ohms,
                            const C8 *data_format,
                            const C8 *freq_format,
                            S32       DC_entry,
                            const C8 *explicit_filename)
{
    QElapsedTimer total_timer;
    QElapsedTimer timer;
    ViStatus stat;
    ViByte data[512] = { 0 };
    ViUInt32 retCount;

    qDebug("save_SnP_FORM4() start");
    total_timer.start();
    //
    // Get filename to save
    //
    C8 filename[MAX_PATH + 1] = { 0 };
    if ((explicit_filename != nullptr) && (explicit_filename[0]))
    {
        strncpy(filename, explicit_filename, MAX_PATH);
    }
    else
    {
        return FALSE;
    }

    //
    // Force filename to end in .SnP suffix
    //
    S32 l = strlen(filename);
    if (l >= 4)
    {
        if (SnP == 1)
        {
            if (_stricmp(&filename[l - 4], ".S1P"))
            {
                strcat(filename, ".S1P");
            }
        }
        else
        {
            if (_stricmp(&filename[l - 4], ".S2P"))
            {
                strcat(filename, ".S2P");
            }
        }
    }
    this->savefile_path = QFileInfo(filename).path(); // store dir path

    /* Measyre time for debug/optimizations ... */
    qDebug("timer.clockType()=%d ", timer.clockType());

    timer.start();
    qDebug("instrument_setup() start");
    if(instrument_setup(instr) == FALSE)
    {
        qDebug("instrument_setup(instr) error\n");
        return FALSE;
    }
    qDebug("instrument_setup() end time=%lld ms\n", timer.elapsed());

    //
    // Get start/stop freq and # of trace points
    //
    S32    n = 0;
    DOUBLE start_Hz = 0.0;
    DOUBLE stop_Hz = 0.0;

    qDebug("STAR/STOP/POIN? queries start");
    timer.start();

    // STAR/STOP/POIN? queries
    stat = viPrintf(instr, (ViString)"FORM4;STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);

    DOUBLE fn = 0.0;
    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &fn);
    qDebug("viScanf() fn=%lf stat=%d", fn, stat);

    qDebug("STAR/STOP/POIN? queries end time=%lld ms\n", timer.elapsed());

    n = (S32)(fn + 0.5);
    if ((n < 1) || (n > 1000000))
    {
        qDebug("Error n_points = %d\n", n);
        return FALSE;
    }

    //
    // Reserve space for DC term if requested
    //
    bool include_DC = (DC_entry != 0);
    S32 n_alloc_points = n;
    S32 n_AC_points = n;
    S32 first_AC_point = 0;

    if (include_DC)
    {
        n_alloc_points++;
        first_AC_point = 1;
    }

    DOUBLE *freq_Hz = (DOUBLE *)alloca(n_alloc_points * sizeof(freq_Hz[0])); memset(freq_Hz, 0, n_alloc_points * sizeof(freq_Hz[0]));

    COMPLEX_DOUBLE *S11 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S11[0])); memset(S11, 0, n_alloc_points * sizeof(S11[0]));
    COMPLEX_DOUBLE *S21 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S21[0])); memset(S21, 0, n_alloc_points * sizeof(S21[0]));
    COMPLEX_DOUBLE *S12 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S12[0])); memset(S12, 0, n_alloc_points * sizeof(S12[0]));
    COMPLEX_DOUBLE *S22 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S22[0])); memset(S22, 0, n_alloc_points * sizeof(S22[0]));

    if (include_DC)
    {
        S11[0].real = 1.0;
        S21[0].real = 1.0;
        S12[0].real = 1.0;
        S22[0].real = 1.0;
    }

    //
    // Construct frequency array
    //
    // For non-8510 analyzers, if LINFREQ? indicates a linear sweep is in use, we construct
    // the array directly.  If a nonlinear sweep is in use, we obtain the frequencies from
    // an OUTPLIML query (08753-90256 example 3B).
    //
    // Note that the frequency parameter in .SnP files taken in POWS or CWTIME mode
    // will reflect the power or time at each point, rather than the CW frequency
    //
    qDebug("Frequency array queries start");
    timer.start();
    bool lin_sweep = TRUE;
    stat = viPrintf(instr, (ViString)"LINFREQ?;\n");
    qDebug("LINFREQ?; stat=%d", stat);
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", data[0], retCount, stat);
    lin_sweep = (data[0] == '1');

    if (lin_sweep)
    {
        for (S32 i = 0; i < n_AC_points; i++)
        {
            freq_Hz[i + first_AC_point] = start_Hz + (((stop_Hz - start_Hz) * i) / (n_AC_points - 1));
        }
    }
    else
    {
        stat = viPrintf(instr, (ViString)"OUTPLIML;\n");
        qDebug("OUTPLIML; stat=%d", stat);

        for (S32 i = 0; i < n_AC_points; i++)
        {
            DOUBLE f = DBL_MIN;

            stat = viScanf(instr,(ViString)"%lf", &f);
            qDebug("viScanf() f=%lf stat=%d", f, stat);

            if (f == DBL_MIN)
            {
                qDebug("Error VNA read timed out reading OUTPLIML (point %d of %d points)", i, n_AC_points);
                return FALSE;
            }
            freq_Hz[i + first_AC_point] = f;

            qDebug("Progress %d%%", 5 + (i * 5 / n_AC_points));
            progress->setValue(5 + (i * 5 / n_AC_points));
            QApplication::processEvents(); // Force refresh process all events
        }
    }
    qDebug("Frequency array queries end time=%lld ms\n", timer.elapsed());

    //
    // If this is an 8753 or 8720, determine what the active parameter is so it can be
    // restored afterward
    // (S12 and S22 queries are not supported on 8752 or 8510)
    //
    qDebug("Active parameter queries start");
    timer.start();
    S32 active_param = 0;
    C8 param_names[4][4] = { "S11", "S21", "S12", "S22" };
    for (active_param = 0; active_param < 4; active_param++)
    {
        C8 text[512] = { 0 };
        _snprintf(text, sizeof(text) - 1, "%s?", param_names[active_param]);

        stat = viPrintf(instr, (ViString)"%s\n", text);
        qDebug("%s stat=%d", stat);
        // Read the 1 when complete
        memset(data, 0, 2);
        stat = viRead(instr, data, 2, &retCount);
        qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d time=%lld ms", data[0], retCount, stat, timer.elapsed());
        if (data[0] == '1')
        {
            break;
        }
    }
    qDebug("Active parameter queries end time=%lld ms\n", timer.elapsed());

    qDebug("Progress %d%%\n", 15);
    progress->setValue(15);
    QApplication::processEvents(); // Force refresh process all events
    //
    // Read data from VNA
    //
    bool result = FALSE;
    if (progress->wasCanceled())
    {
        stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
        qDebug("DEBUOFF;CONT; stat=%d", stat);
        return FALSE;
    }
    if (SnP == 1)
    {
        qDebug("read_complex_trace_FORM4 start %s", param);
        timer.start();
        result = read_complex_trace_FORM4(progress, instr, param, query, &S11[first_AC_point], n_AC_points, 50);
        qDebug("read_complex_trace_FORM4 end %s result=%d time=%lld ms\n", param, result, timer.elapsed());
    }
    else
    {
        qDebug("read_complex_trace_FORM4 S11, S21, S12, S22 start\n");

        qDebug(" read_complex_trace_FORM4 S11 start");
        timer.start();
        result = read_complex_trace_FORM4(progress, instr, (C8*)"S11", query, &S11[first_AC_point], n_AC_points, 20);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM4 S11 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM4 S21 start");
        timer.start();
        result = result && read_complex_trace_FORM4(progress, instr, (C8*)"S21", query, &S21[first_AC_point], n_AC_points, 40);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug(" DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM4 S21 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM4 S12 start");
        timer.start();
        result = result && read_complex_trace_FORM4(progress, instr, (C8*)"S12", query, &S12[first_AC_point], n_AC_points, 60);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM4 S12 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM4 S22 start");
        timer.start();
        result = result && read_complex_trace_FORM4(progress, instr, (C8*)"S22", query, &S22[first_AC_point], n_AC_points, 80);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM4 S22 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug("read_complex_trace_FORM4 S11, S21, S12, S22 end result=%d\n", result);
    }

    if (progress->wasCanceled())
    {
        stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
        qDebug("DEBUOFF;CONT; stat=%d", stat);
        return FALSE;
    }

    //
    // Create S-parameter database, fill it with received data, and save it
    //
    qDebug("Create S-parameter start");
    timer.start();
    if (result)
    {
        SPARAMS S;

        if (!S.alloc(SnP, n_alloc_points))
        {
            qDebug("Error %s", S.message_text);
        }
        else
        {
            S.min_Hz = include_DC ? 0.0 : start_Hz;
            S.max_Hz = stop_Hz;
            S.Zo = R_ohms;

            for (S32 i = 0; i < n_alloc_points; i++)
            {
                S.freq_Hz[i] = freq_Hz[i];
                if (SnP == 1)
                {
                    // TODO, when sparams.cpp supports single-param files other than S11...
                    //               if (param[1] == '1')
                    { S.RI[0][0][i] = S11[i]; S.valid[0][0][i] = SNPTYPE::RI; }
                    //               else
                    //                  { S.RI[1][1][i] = S22[i]; S.valid[1][1][i] = SNPTYPE::RI; }
                }
                else
                {
                    S.RI[0][0][i] = S11[i]; S.valid[0][0][i] = SNPTYPE::RI;
                    S.RI[1][0][i] = S21[i]; S.valid[1][0][i] = SNPTYPE::RI;
                    S.RI[0][1][i] = S12[i]; S.valid[0][1][i] = SNPTYPE::RI;
                    S.RI[1][1][i] = S22[i]; S.valid[1][1][i] = SNPTYPE::RI;
                }
            }

            C8 header[1024] = { 0 };
            /* Obtain current time. */
            time_t current_time = time(nullptr);
            /* Convert to local time format. */
            char last_char;
            char* c_time_string = ctime(&current_time);
            last_char = c_time_string[strlen(c_time_string)-1];
            if ( (last_char == '\n') || (last_char == '\r'))
            {
                c_time_string[strlen(c_time_string)-1] = 0;
            }
            last_char = c_time_string[strlen(c_time_string)-1];
            if ( (last_char == '\n') || (last_char == '\r'))
            {
                c_time_string[strlen(c_time_string)-1] = 0;
            }

            _snprintf(header, sizeof(header) - 1,
                "! Touchstone 1.1 file saved by VNA QT V%s\n"
                "! %s\n"
                "!\n"
                "! %s OPT: %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n",
                      VER_FILEVERSION_STR,
                      c_time_string,
                      instrument_name, instrument_opts,
                      instrument_if_bandwidth,
                      instrument_out_power_level,
                      instrument_smoothing,
                      instrument_averaging,
                      instrument_correction);
            if (!S.write_SNP_file(filename, data_format, freq_format, header, param))
            {
                qDebug("Error %s", S.message_text);
            }
        }
        qDebug("Create S-parameter end time=%lld ms\n", timer.elapsed());
    }else {
        qDebug("read_complex_trace_FORM4() error\n");
    }

    //
    // Restore active parameter and exit
    //
    qDebug("Restore active parameter start");
    timer.start();
    if (active_param <= 3)
    {
        stat = viPrintf(instr, (ViString)"%s\n", param_names[active_param]);
        qDebug("%s stat=%d", param_names[active_param], stat);
    }

    stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
    qDebug("DEBUOFF;CONT; stat=%d", stat);

    qDebug("Restore active parameter end time=%lld ms\n", timer.elapsed());

    qDebug("Progress %d%%\n", 100);
    progress->setValue(100);
    QApplication::processEvents(); // Force refresh process all events

    qint64 total_time_ms = total_timer.elapsed();
    qDebug("save_SnP_FORM4()) end total_time=%lld seconds (%lld ms)\n", total_time_ms/1000, total_time_ms);
    return TRUE;
}

/*
Parameters:
ViSession instr =>Visa Session
C8             *param => "S11" or "S21" or "S12" or "S22"
C8             *query => "OUTPDATA" (Default) or "OUTPFORM"
COMPLEX_DOUBLE *dest 	=> dest data
S32             cnt		=> number of points (n_AC_points)
S32             progress_fraction => Progression in %
*/
bool MainWindow::read_complex_trace_FORM1(QProgressDialog *progress,
                                    ViSession instr,
                                    C8             *param,
                                    C8             *query,
                                    COMPLEX_DOUBLE *dest,
                                    S32             cnt,
                                    S32             progress_fraction)
{
    ViByte buf[65536] = { 0 };
    ViUInt32 retCount;
    ViStatus stat;
    U8 mask = 0x40;
    QElapsedTimer timer;
    QElapsedTimer timer_readdata;
    int datalen;
    t_form1_raw_imag_real *data_in;

    qDebug(" read_complex_trace_FORM1() start param=%s query=%s", param, query);
    timer.start();

    viPrintf(instr, (ViString)"CLES;SRE 4;ESNB 1;\n");
    qDebug(" viPrintf(\"CLES;SRE 4;ESNB 1;\")");
    mask = 0x40;// Extended register bit 0 = SING sweep complete; map it to status bit and enable SRQ on it

    viPrintf(instr, (ViString)"%s;FORM1;OPC?;SING;\n", param);
    qDebug(" viPrintf(\"%s;FORM1;OPC?;SING;\") time=%lld ms", param, timer.elapsed());
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug(" end param viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d time=%lld ms", buf[0], retCount, stat, timer.elapsed());

    viPrintf(instr, (ViString)"CLES;SRE 0;\n");
    viPrintf(instr, (ViString)"%s;\n", query);

    // Read in the data header two characters and two bytes for length
    // Read header as 2 byte string
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() hdr 2bytes=\"%s\"(expected \"#A\") stat=%d", buf, stat);
    // Read length as 2 bytes integer
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    datalen = (buf[0] << 8) + buf[1]; /* Big Endian Format */
    qDebug("viRead() length 2bytes=0x%02X 0x%02X=>datalen=%d retCount=%d stat=%d", buf[0], buf[1], datalen, retCount, stat);

    // Read trace data
    qDebug("viRead() all trace data (max size=%d)", sizeof(buf));
    timer_readdata.start();
    stat = viRead(instr, buf, sizeof(buf), &retCount);
    qDebug("viRead() stat=%d retCount=%d timer_readdata=%ld ms", stat, retCount, timer_readdata.elapsed());

    retCount /= 6; /* Number of points is size / 6 (6bytes per points) */
    if(retCount != cnt)
    {
        qDebug(" Error retCount(%d) != cnt(%d)", retCount, cnt);
        return FALSE;
    }

    data_in = (t_form1_raw_imag_real*)buf;
    qDebug(" loop start 0 to %d", cnt);
    timer.start();
    for (S32 i = 0; i < cnt; i++)
    {
        DOUBLE I = DBL_MIN;
        DOUBLE Q = DBL_MIN;

        conv_form1_real_imag(&data_in[i], &I, &Q);
        if ((I == DBL_MIN) || (Q == DBL_MIN))
        {
            qDebug(" Error VNA read timed out reading %s (point %d of %d points)", param, i, cnt);
            return FALSE;
        }
        dest[i].real = I;
        dest[i].imag = Q;

        //qDebug("Progress %d%%\n", ((i * 20) / cnt) + progress_fraction);
        progress->setValue(((i * 20) / cnt) + progress_fraction);
        QApplication::processEvents(); // Force refresh process all events
    }
    qDebug(" read_complex_trace_FORM1() loop end time=%lld ms", timer.elapsed());

    return TRUE;
}

/*
save_SnP_FORM1
Parameters:
ViSession instr =>Visa Session
S32 SnP => 1 = S1P or 2 = S2P
C8 *param => "" for S2P, "S11", "S21" or "S22" for S1P
C8 *query => "OUTPDATA" (Default) or "OUTPFORM"
DOUBLE R_ohms => 50.0
const C8 *data_format => S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
const C8 *freq_format => "Hz"(Default), "kHz", "MHz", "GHz"
S32 DC_entry => 0 = None(Default)
const C8 *explicit_filename => Output filename
*/
bool MainWindow::save_SnP_FORM1(  QProgressDialog* progress,
                            ViSession instr,
                            S32       SnP,
                            C8       *param,
                            C8       *query,
                            DOUBLE    R_ohms,
                            const C8 *data_format,
                            const C8 *freq_format,
                            S32       DC_entry,
                            const C8 *explicit_filename)
{
    QElapsedTimer total_timer;
    QElapsedTimer timer;
    ViStatus stat;
    ViByte data[512] = { 0 };
    ViUInt32 retCount;

    qDebug("save_SnP_FORM1() start");
    total_timer.start();
    //
    // Get filename to save
    //
    C8 filename[MAX_PATH + 1] = { 0 };
    if ((explicit_filename != nullptr) && (explicit_filename[0]))
    {
        strncpy(filename, explicit_filename, MAX_PATH);
    }
    else
    {
        return FALSE;
    }

    //
    // Force filename to end in .SnP suffix
    //
    S32 l = strlen(filename);
    if (l >= 4)
    {
        if (SnP == 1)
        {
            if (_stricmp(&filename[l - 4], ".S1P"))
            {
                strcat(filename, ".S1P");
            }
        }
        else
        {
            if (_stricmp(&filename[l - 4], ".S2P"))
            {
                strcat(filename, ".S2P");
            }
        }
    }
    this->savefile_path = QFileInfo(filename).path(); // store dir path

    /* Measure time for debug/optimizations ... */
    qDebug("timer.clockType()=%d ", timer.clockType());

    timer.start();
    qDebug("instrument_setup() start");
    if(instrument_setup(instr) == FALSE)
    {
        qDebug("instrument_setup(instr) error\n");
        return FALSE;
    }
    qDebug("instrument_setup() end time=%lld ms\n", timer.elapsed());

    //
    // Get start/stop freq and # of trace points
    //
    S32    n = 0;
    DOUBLE start_Hz = 0.0;
    DOUBLE stop_Hz = 0.0;

    qDebug("STAR/STOP/POIN? queries start");
    timer.start();

    // STAR/STOP/POIN? queries
    stat = viPrintf(instr, (ViString)"FORM4;STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);

    DOUBLE fn = 0.0;
    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &fn);
    qDebug("viScanf() fn=%lf stat=%d", fn, stat);

    qDebug("STAR/STOP/POIN? queries end time=%lld ms\n", timer.elapsed());

    n = (S32)(fn + 0.5);
    if ((n < 1) || (n > 1000000))
    {
        qDebug("Error n_points = %d\n", n);
        return FALSE;
    }

    //
    // Reserve space for DC term if requested
    //
    bool include_DC = (DC_entry != 0);
    S32 n_alloc_points = n;
    S32 n_AC_points = n;
    S32 first_AC_point = 0;

    if (include_DC)
    {
        n_alloc_points++;
        first_AC_point = 1;
    }

    DOUBLE *freq_Hz = (DOUBLE *)alloca(n_alloc_points * sizeof(freq_Hz[0])); memset(freq_Hz, 0, n_alloc_points * sizeof(freq_Hz[0]));

    COMPLEX_DOUBLE *S11 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S11[0])); memset(S11, 0, n_alloc_points * sizeof(S11[0]));
    COMPLEX_DOUBLE *S21 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S21[0])); memset(S21, 0, n_alloc_points * sizeof(S21[0]));
    COMPLEX_DOUBLE *S12 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S12[0])); memset(S12, 0, n_alloc_points * sizeof(S12[0]));
    COMPLEX_DOUBLE *S22 = (COMPLEX_DOUBLE *)alloca(n_alloc_points * sizeof(S22[0])); memset(S22, 0, n_alloc_points * sizeof(S22[0]));

    if (include_DC)
    {
        S11[0].real = 1.0;
        S21[0].real = 1.0;
        S12[0].real = 1.0;
        S22[0].real = 1.0;
    }

    //
    // Construct frequency array
    //
    // For non-8510 analyzers, if LINFREQ? indicates a linear sweep is in use, we construct
    // the array directly.  If a nonlinear sweep is in use, we obtain the frequencies from
    // an OUTPLIML query (08753-90256 example 3B).
    //
    // Note that the frequency parameter in .SnP files taken in POWS or CWTIME mode
    // will reflect the power or time at each point, rather than the CW frequency
    //
    qDebug("Frequency array queries start");
    timer.start();
    bool lin_sweep = TRUE;
    stat = viPrintf(instr, (ViString)"LINFREQ?;\n");
    qDebug("LINFREQ?; stat=%d", stat);
    // Read the 1 when complete
    memset(data, 0, 2);
    stat = viRead(instr, data, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", data[0], retCount, stat);
    lin_sweep = (data[0] == '1');

    if (lin_sweep)
    {
        for (S32 i = 0; i < n_AC_points; i++)
        {
            freq_Hz[i + first_AC_point] = start_Hz + (((stop_Hz - start_Hz) * i) / (n_AC_points - 1));
        }
    }
    else
    {
        stat = viPrintf(instr, (ViString)"OUTPLIML;\n");
        qDebug("OUTPLIML; stat=%d", stat);

        for (S32 i = 0; i < n_AC_points; i++)
        {
            DOUBLE f = DBL_MIN;

            stat = viScanf(instr,(ViString)"%lf", &f);
            qDebug("viScanf() f=%lf stat=%d", f, stat);

            if (f == DBL_MIN)
            {
                qDebug("Error VNA read timed out reading OUTPLIML (point %d of %d points)", i, n_AC_points);
                return FALSE;
            }
            freq_Hz[i + first_AC_point] = f;

            qDebug("Progress %d%%", 5 + (i * 5 / n_AC_points));
            progress->setValue(5 + (i * 5 / n_AC_points));
            QApplication::processEvents(); // Force refresh process all events
        }
    }
    qDebug("Frequency array queries end time=%lld ms\n", timer.elapsed());

    //
    // If this is an 8753 or 8720, determine what the active parameter is so it can be
    // restored afterward
    // (S12 and S22 queries are not supported on 8752 or 8510)
    //
    qDebug("Active parameter queries start");
    timer.start();
    S32 active_param = 0;
    C8 param_names[4][4] = { "S11", "S21", "S12", "S22" };
    for (active_param = 0; active_param < 4; active_param++)
    {
        C8 text[512] = { 0 };
        _snprintf(text, sizeof(text) - 1, "%s?", param_names[active_param]);

        stat = viPrintf(instr, (ViString)"%s\n", text);
        qDebug("%s stat=%d", text, stat);
        // Read the 1 when complete
        memset(data, 0, 2);
        stat = viRead(instr, data, 2, &retCount);
        qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d time=%lld ms", data[0], retCount, stat, timer.elapsed());
        if (data[0] == '1')
        {
            break;
        }
    }
    qDebug("Active parameter queries end time=%lld ms\n", timer.elapsed());

    qDebug("Progress %d%%\n", 15);
    progress->setValue(15);
    QApplication::processEvents(); // Force refresh process all events
    //
    // Read data from VNA
    //
    bool result = FALSE;
    if (progress->wasCanceled())
    {
        stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
        qDebug("DEBUOFF;CONT; stat=%d", stat);
        return FALSE;
    }
    if (SnP == 1)
    {
        qDebug("read_complex_trace_FORM1 start %s", param);
        timer.start();
        result = read_complex_trace_FORM1(progress, instr, param, query, &S11[first_AC_point], n_AC_points, 50);
        qDebug("read_complex_trace_FORM1 end %s result=%d time=%lld ms\n", param, result, timer.elapsed());
    }
    else
    {
        qDebug("read_complex_trace_FORM1 S11, S21, S12, S22 start\n");

        qDebug(" read_complex_trace_FORM1 S11 start");
        timer.start();
        result = read_complex_trace_FORM1(progress, instr, (C8*)"S11", query, &S11[first_AC_point], n_AC_points, 20);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM1 S11 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM1 S21 start");
        timer.start();
        result = result && read_complex_trace_FORM1(progress, instr, (C8*)"S21", query, &S21[first_AC_point], n_AC_points, 40);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug(" DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM1 S21 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM1 S12 start");
        timer.start();
        result = result && read_complex_trace_FORM1(progress, instr, (C8*)"S12", query, &S12[first_AC_point], n_AC_points, 60);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM1 S12 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug(" read_complex_trace_FORM1 S22 start");
        timer.start();
        result = result && read_complex_trace_FORM1(progress, instr, (C8*)"S22", query, &S22[first_AC_point], n_AC_points, 80);
        if (progress->wasCanceled())
        {
            stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
            qDebug("DEBUOFF;CONT; stat=%d", stat);
            return FALSE;
        }
        qDebug(" read_complex_trace_FORM1 S22 end result=%d time=%lld ms\n", result, timer.elapsed());

        qDebug("read_complex_trace_FORM1 S11, S21, S12, S22 end result=%d\n", result);
    }

    if (progress->wasCanceled())
    {
        stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
        qDebug("DEBUOFF;CONT; stat=%d", stat);
        return FALSE;
    }

    //
    // Create S-parameter database, fill it with received data, and save it
    //
    qDebug("Create S-parameter start");
    QApplication::processEvents(); // Force refresh process all events
    timer.start();
    if (result)
    {
        SPARAMS S;
        if (!S.alloc(SnP, n_alloc_points))
        {
            qDebug("Error %s", S.message_text);
        }
        else
        {
            S.min_Hz = include_DC ? 0.0 : start_Hz;
            S.max_Hz = stop_Hz;
            S.Zo = R_ohms;

            for (S32 i = 0; i < n_alloc_points; i++)
            {
                S.freq_Hz[i] = freq_Hz[i];
                if (SnP == 1)
                {
                    // TODO, when sparams.cpp supports single-param files other than S11...
                    //               if (param[1] == '1')
                    { S.RI[0][0][i] = S11[i]; S.valid[0][0][i] = SNPTYPE::RI; }
                    //               else
                    //                  { S.RI[1][1][i] = S22[i]; S.valid[1][1][i] = SNPTYPE::RI; }
                }
                else
                {
                    S.RI[0][0][i] = S11[i]; S.valid[0][0][i] = SNPTYPE::RI;
                    S.RI[1][0][i] = S21[i]; S.valid[1][0][i] = SNPTYPE::RI;
                    S.RI[0][1][i] = S12[i]; S.valid[0][1][i] = SNPTYPE::RI;
                    S.RI[1][1][i] = S22[i]; S.valid[1][1][i] = SNPTYPE::RI;
                }
            }

            C8 header[1024] = { 0 };
            /* Obtain current time. */
            time_t current_time = time(nullptr);
            /* Convert to local time format. */
            char last_char;
            char* c_time_string = ctime(&current_time);
            last_char = c_time_string[strlen(c_time_string)-1];
            if ( (last_char == '\n') || (last_char == '\r'))
            {
                c_time_string[strlen(c_time_string)-1] = 0;
            }
            last_char = c_time_string[strlen(c_time_string)-1];
            if ( (last_char == '\n') || (last_char == '\r'))
            {
                c_time_string[strlen(c_time_string)-1] = 0;
            }

            _snprintf(header, sizeof(header) - 1,
                "! Touchstone 1.1 file saved by VNA QT V%s\n"
                "! %s\n"
                "!\n"
                "! %s OPT: %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n"
                "! %s\n",
                      VER_FILEVERSION_STR,
                      c_time_string,
                      instrument_name, instrument_opts,
                      instrument_if_bandwidth,
                      instrument_out_power_level,
                      instrument_smoothing,
                      instrument_averaging,
                      instrument_correction);
            if (!S.write_SNP_file(filename, data_format, freq_format, header, param))
            {
                qDebug("Error %s", S.message_text);
            }
        }
        qDebug("Create S-parameter end time=%lld ms\n", timer.elapsed());
    }else {
        qDebug("read_complex_trace_FORM1() error\n");
    }

    //
    // Restore active parameter and exit
    //
    qDebug("Restore active parameter start");
    QApplication::processEvents(); // Force refresh process all events
    timer.start();
    if (active_param <= 3)
    {
        stat = viPrintf(instr, (ViString)"%s\n", param_names[active_param]);
        qDebug("%s stat=%d", param_names[active_param], stat);
    }

    stat = viPrintf(instr, (ViString)"DEBUOFF;CONT;\n");
    qDebug("DEBUOFF;CONT; stat=%d", stat);

    qDebug("Restore active parameter end time=%lld ms\n", timer.elapsed());

    qDebug("Progress %d%%\n", 100);
    progress->setValue(100);
    qint64 total_time_ms = total_timer.elapsed();
    qDebug("save_SnP_FORM1()) end total_time=%lld seconds (%lld ms)\n", total_time_ms/1000, total_time_ms);
    QApplication::processEvents(); // Force refresh process all events

    if (result)
    {
        return TRUE;
    }else
    {
        return FALSE;
    }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QString title_ver_info;

    // Load the embedded font.
    QString fontPath = ":/fonts/LiberationSans-Regular.ttf";
    QFontDatabase::addApplicationFont(fontPath);
    QFont font("Liberation Sans",8);
    this->setFont(font);
    QFontDatabase db1;

    title_ver_info = "VNA_Qt for HP8753 v";
    title_ver_info.append(VER_FILEVERSION_STR);
    title_ver_info.append(VER_DATE_INFO_STR);
    title_ver_info.append(" (Based on Qt");
    title_ver_info.append(QT_VERSION_STR);
    title_ver_info.append("-");
    title_ver_info.append(QStringLiteral("%1bits)").arg(Q_PROCESSOR_WORDSIZE * 8));

    ui->setupUi(this);

    /* Hide test for buttons used to check FORM1, 4 & 5 data */
    ui->pushButtonFORM1->setVisible(false);
    ui->pushButtonFORM4->setVisible(false);
    ui->pushButtonFORM5->setVisible(false);

    /* Hide  button SnP with FORM4 as FORM1 is the default one which is more compact/faster */
    ui->pushButtonSnP_FORM4->setVisible(false);

    this->setWindowTitle(title_ver_info);

    //readSettings();
}

MainWindow::~MainWindow()
{
    //writeSettings();
    delete ui;
}

void MainWindow::readSettings()
{
    QSettings settings(SETTINGS_FILENAME, QSettings::IniFormat);
    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    settings.endGroup();
    settings.beginGroup("SavePath");
    this->savefile_path = settings.value("savefile_path", "").toString();
    //this->ui->filepath->setText(this->savefile_path);
    settings.endGroup();	
}

void MainWindow::writeSettings()
{
    QSettings settings(SETTINGS_FILENAME, QSettings::IniFormat);
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();
    settings.beginGroup("SavePath");
    settings.setValue("savefile_path", this->savefile_path);
    settings.endGroup();	
}

void MainWindow::on_pushButtonSnP_FORM4_clicked()
{
    QElapsedTimer timer;
    qint64 time_elapsed_ms;
    char data[512];
    char filename[MAX_PATH + 1];
    ViByte buf[256] = { 0 };
    ViUInt32 retCount;

    // open resource manager
    ViSession rscmng;

    qDebug () << "on_pushButtonSnP_FORM4_clicked start";

    /* Read GUI configuration  */
    S32 SnP; // 1 = S1P or 2 = S2P
    C8 *param; // "" for S2P, "S11", "S21" or "S22" for S1P
    C8 *query; // "OUTPDATA" (Default) or "OUTPFORM"
    DOUBLE R_ohms = 50.0;
    C8 *data_format; // S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
    C8 *freq_format; // "Hz"(Default), "kHz", "MHz", "GHz"
    S32 DC_entry = 0; // 0 = None(Default)

    char param_S2P_ALL[] = "";
    char param_S1P_S11[] = "S11";
    char param_S1P_S21[] = "S21";
    char param_S1P_S22[] = "S22";

    char data_format_MA[] = "MA";
    char data_format_DB[] = "DB";
    char data_format_RI[] = "RI";

    char freq_format_Hz[] = "Hz";
    char freq_format_kHz[] = "kHz";
    char freq_format_MHz[] = "MHz";
    char freq_format_GHz[] = "GHz";

    #define QUERY_STR_LEN (32)
    char query_str[QUERY_STR_LEN+1] = { 0 };

		// In .S1P file S12 is missing (in theory the same as S21)
    switch(this->ui->comboBoxSnP_FileType->currentIndex())
    {
        case 0: // .S2P (ALL)
            SnP = 2;
            param = param_S2P_ALL;
        break;

        case 1: // .S1P (S11)
            SnP = 1;
            param = param_S1P_S11;
        break;

        case 2: // .S1P (S21)
            SnP = 1;
            param = param_S1P_S21;
        break;

        case 3: // .S1P (S22)
            SnP = 1;
            param = param_S1P_S22;
        break;

        default: // .S2P (ALL)
            SnP = 2;
            param = param_S2P_ALL;
        break;
    }

    _snprintf(query_str, QUERY_STR_LEN, this->ui->comboBoxSnP_Query->currentText().toStdString().c_str());
    query = query_str;

    //data_format
    if(this->ui->radioButtonSnP_MA->isChecked() == true)
        data_format = data_format_MA;

    if(this->ui->radioButtonSnP_DB->isChecked() == true)
        data_format = data_format_DB;

    if(this->ui->radioButtonSnP_RI->isChecked() == true)
        data_format = data_format_RI;

    switch(this->ui->comboBoxSnP_Freq->currentIndex())
    {
        case 0: // Hz
            freq_format = freq_format_Hz;
        break;

        case 1: // kHz
            freq_format = freq_format_kHz;
        break;

        case 2: // MHz
            freq_format = freq_format_MHz;
        break;

        case 3: // GHz
            freq_format = freq_format_GHz;
        break;

        default: // Hz
            freq_format = freq_format_Hz;
        break;
    }

    DC_entry = this->ui->comboBoxSnP_DC->currentIndex();

    qDebug("SnP=%d param=%s query=%s R_ohms=%lf data_format=%s freq_format=%s DC_entry=%d",
           SnP, param, query, R_ohms, data_format, freq_format, DC_entry);

    ViStatus stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);
/*
    // search for the VNA
    ViChar viFound[VI_FIND_BUFLEN] = { 0 };
    ViUInt32 nFound;
    ViFindList listOfFound;
    stat = viFindRsrc(rscmng, (ViString)"GPIB?*INSTR", &listOfFound, &nFound, viFound);
    //stat = viFindRsrc(rscmng, (ViString)"GPIB?*", &listOfFound, &nFound, viFound);
    qDebug("viFindRsrc stat=%d listOfFound=%d nFound=%d", stat, listOfFound, nFound);
    qDebug("viFindRsrc viFound=%s", viFound);
*/
    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    QString savefile_caption;
    QString savefile_filter;
    QString progress_label;
    if (SnP == 1)
    {
        savefile_caption += "Save Touchstone .S1P file";
        savefile_filter += "S1P files (*.S1P);;All files (*.*)";
        progress_label += "Capture S-Parameter in progress...";
    } else
    {
        savefile_caption += "Save Touchstone .S2P file";
        savefile_filter += "S2P files (*.S2P);;All files (*.*)";
        progress_label += "Capture S-Parameters in progress...";
    }
	
	if(this->savefile_path.length() == 0)
    {
        this->savefile_path = QDir::currentPath();
    }
	
    QString qfilename = QFileDialog::getSaveFileName(this, savefile_caption, this->savefile_path, savefile_filter);
    if(!qfilename.length())
        return;

	this->savefile_path = QFileInfo(qfilename).path(); // store path for next time

    strncpy(filename, qfilename.toStdString().c_str(), MAX_PATH);
    qDebug("filename = \"%s\"", filename);
    QProgressDialog progress(progress_label, "Cancel", 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setValue(0);
    progress.repaint();

    bool res;
    timer.start();
    res = save_SnP_FORM4(&progress,
                   instr, // Visa Session
                   SnP, // S32 SnP => 1 = S1P or 2 = S2P
                   (C8*)param, // "" for S2P, "S11", "S21" or "S22" for S1P
                   (C8*)query, // "OUTPDATA" (Default) or "OUTPFORM"
                   R_ohms, // DOUBLE R_ohms default 50.0
                   (C8*)data_format, // S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
                   (C8*)freq_format, // "Hz"(Default), "kHz", "MHz", "GHz"
                   DC_entry, // 0 = None(Default)
                   filename);
    time_elapsed_ms = timer.elapsed();
    if(res == TRUE)
    {
        sprintf(data, "save_SnP_FORM4() finished with success in %lld s(%lld ms) see file %s\n", time_elapsed_ms/1000, time_elapsed_ms, filename);
        qDebug("%s", data);
    }else
    {
        sprintf(data, "save_SnP_FORM4() finished with error\n");
        qDebug("%s", data);
    }
    this->ui->plainTextEdit->appendPlainText(data);
    progress.setValue(100);

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButtonSnP_FORM4_clicked exit";
}

void MainWindow::on_pushButtonSnP_FORM1_clicked()
{
    QElapsedTimer timer;
    qint64 time_elapsed_ms;
    char data[512];
    char filename[MAX_PATH + 1];
    ViByte buf[256] = { 0 };
    ViUInt32 retCount;

    // open resource manager
    ViSession rscmng;

    qDebug () << "on_pushButtonSnP_FORM1_clicked start";

    /* Read GUI configuration  */
    S32 SnP; // 1 = S1P or 2 = S2P
    C8 *param; // "" for S2P, "S11", "S21" or "S22" for S1P
    C8 *query; // "OUTPDATA" (Default) or "OUTPFORM"
    DOUBLE R_ohms = 50.0;
    C8 *data_format; // S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
    C8 *freq_format; // "Hz"(Default), "kHz", "MHz", "GHz"
    S32 DC_entry = 0; // 0 = None(Default)

    char param_S2P_ALL[] = "";
    char param_S1P_S11[] = "S11";
    char param_S1P_S21[] = "S21";
    char param_S1P_S22[] = "S22";

    char data_format_MA[] = "MA";
    char data_format_DB[] = "DB";
    char data_format_RI[] = "RI";

    char freq_format_Hz[] = "Hz";
    char freq_format_kHz[] = "kHz";
    char freq_format_MHz[] = "MHz";
    char freq_format_GHz[] = "GHz";

    #define QUERY_STR_LEN (32)
    char query_str[QUERY_STR_LEN+1] = { 0 };

    switch(this->ui->comboBoxSnP_FileType->currentIndex())
    {
        case 0: // .S2P (ALL)
            SnP = 2;
            param = param_S2P_ALL;
        break;

        case 1: // .S1P (S11)
            SnP = 1;
            param = param_S1P_S11;
        break;

        case 2: // .S1P (S21)
            SnP = 1;
            param = param_S1P_S21;
        break;

        case 3: // .S1P (S22)
            SnP = 1;
            param = param_S1P_S22;
        break;

        default: // .S2P (ALL)
            SnP = 2;
            param = param_S2P_ALL;
        break;
    }

    _snprintf(query_str, QUERY_STR_LEN, this->ui->comboBoxSnP_Query->currentText().toStdString().c_str());
    query = query_str;

    //data_format
    if(this->ui->radioButtonSnP_MA->isChecked() == true)
        data_format = data_format_MA;

    if(this->ui->radioButtonSnP_DB->isChecked() == true)
        data_format = data_format_DB;

    if(this->ui->radioButtonSnP_RI->isChecked() == true)
        data_format = data_format_RI;

    switch(this->ui->comboBoxSnP_Freq->currentIndex())
    {
        case 0: // Hz
            freq_format = freq_format_Hz;
        break;

        case 1: // kHz
            freq_format = freq_format_kHz;
        break;

        case 2: // MHz
            freq_format = freq_format_MHz;
        break;

        case 3: // GHz
            freq_format = freq_format_GHz;
        break;

        default: // Hz
            freq_format = freq_format_Hz;
        break;
    }

    DC_entry = this->ui->comboBoxSnP_DC->currentIndex();

    qDebug("SnP=%d param=%s query=%s R_ohms=%lf data_format=%s freq_format=%s DC_entry=%d",
           SnP, param, query, R_ohms, data_format, freq_format, DC_entry);

    ViStatus stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // search for the VNA
    /*
    ViChar viFound[VI_FIND_BUFLEN] = { 0 };
    ViUInt32 nFound;
    ViFindList listOfFound;
    stat = viFindRsrc(rscmng, (ViString)"GPIB?*INSTR", &listOfFound, &nFound, viFound);
    //stat = viFindRsrc(rscmng, (ViString)"GPIB?*", &listOfFound, &nFound, viFound);
    qDebug("viFindRsrc stat=%d listOfFound=%d nFound=%d", stat, listOfFound, nFound);
    qDebug("viFindRsrc viFound=%s", viFound);
    */

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    QString savefile_caption;
    QString savefile_filter;
    QString progress_label;
    if (SnP == 1)
    {
        savefile_caption += "Save Touchstone .S1P file";
        savefile_filter += "S1P files (*.S1P);;All files (*.*)";
        progress_label += "Capture S-Parameter in progress...";
    } else
    {
        savefile_caption += "Save Touchstone .S2P file";
        savefile_filter += "S2P files (*.S2P);;All files (*.*)";
        progress_label += "Capture S-Parameters in progress...";
    }

	if(this->savefile_path.length() == 0)
    {
        this->savefile_path = QDir::currentPath();
    }
	
    QString qfilename = QFileDialog::getSaveFileName(this, savefile_caption, this->savefile_path, savefile_filter);
    if(!qfilename.length())
        return;

	this->savefile_path = QFileInfo(qfilename).path(); // store path for next time

    strncpy(filename, qfilename.toStdString().c_str(), MAX_PATH);
    qDebug("filename = \"%s\"", filename);

    QProgressDialog progress(progress_label, "Cancel", 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setValue(0);
    progress.repaint();

    bool res;
    timer.start();
    res = save_SnP_FORM1(&progress,
                   instr, // Visa Session
                   SnP, // S32 SnP => 1 = S1P or 2 = S2P
                   (C8*)param, // "" for S2P, "S11", "S21" or "S22" for S1P
                   (C8*)query, // "OUTPDATA" (Default) or "OUTPFORM"
                   R_ohms, // DOUBLE R_ohms default 50.0
                   (C8*)data_format, // S2P File Format "MA" Magnitude-angle or "DB" dB-angle or "RI" Real-imaginary
                   (C8*)freq_format, // "Hz"(Default), "kHz", "MHz", "GHz"
                   DC_entry, // 0 = None(Default)
                   filename);
    time_elapsed_ms = timer.elapsed();
    if(res == TRUE)
    {
        sprintf(data, "save_SnP_FORM1() finished with success in %lld s(%lld ms) see file %s\n", time_elapsed_ms/1000, time_elapsed_ms, filename);
        qDebug("%s", data);
    }else
    {
        sprintf(data, "save_SnP_FORM1() finished with error\n");
        qDebug("%s", data);
    }
    progress.setValue(100);
    this->ui->plainTextEdit->appendPlainText(data);

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButtonSnP_FORM1_clicked exit";
}


void MainWindow::on_pushButtonGPIBINFO_clicked()
{
    // open resource manager
    ViSession rscmng;
    ViByte buf[256] = { 0 };
    ViUInt32 retCount;

    qDebug () << "on_pushButtonGPIBINFO_clicked";

    ViStatus stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // search for the VNA
    /*
    ViChar viFound[VI_FIND_BUFLEN] = { 0 };
    ViUInt32 nFound;
    ViFindList listOfFound;
    stat = viFindRsrc(rscmng, (ViString)"GPIB?*INSTR", &listOfFound, &nFound, viFound);
    //stat = viFindRsrc(rscmng, (ViString)"GPIB?*", &listOfFound, &nFound, viFound);
    qDebug("viFindRsrc stat=%d listOfFound=%d nFound=%d", stat, listOfFound, nFound);
    qDebug("viFindRsrc viFound=%s", viFound);
    */

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    instrument_setup(instr);

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);
}

void MainWindow::on_pushButtonPRESET_clicked()
{
    // open resource manager
    ViSession rscmng;
    char debug_info[1024];
    ViByte buf[256] = { 0 };
    ViUInt32 retCount;

    qDebug () << "on_pushButtonPRESET_clicked";

    ViStatus stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    instrument_setup(instr);

    // Preset the analyzer and wait
    stat = viPrintf(instr, (ViString)"OPC?;PRES;\n");
    qDebug("OPC?;PRES; stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    if ((retCount > 0) && (buf[0]=='1'))
    {
        sprintf(debug_info, "HP8753D PRESET completed OK\n");
        this->ui->plainTextEdit->appendPlainText(debug_info);
    }else {
        this->ui->plainTextEdit->appendPlainText("HP8753D PRESET error");
    }

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);
}

void MainWindow::on_pushButtonFORM1_clicked()
{
    // open resource manager
    ViSession rscmng;
    char debug_info[1024];
    ViByte buf[65536] = { 0 };
    ViUInt32 retCount;
    ViStatus stat;
    int datalen;
    char form1_capture_filename[] = { "vna_form1_data.bin" };
    char form5_capture_filename[] = { "vna_form5_PC_FLOAT32.bin" };

    qDebug () << "on_pushButtonFORM1_clicked";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
        QString info = QString("Could not open a session to the VISA Resource Manager!\n");
        qDebug() << info;
        this->ui->plainTextEdit->appendPlainText(info);
        exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc)VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    instrument_setup(instr);

    // Preset the analyzer and wait
/*
    stat = viPrintf(instr, (ViString)"OPC?;PRES;\n");
    qDebug("OPC?;PRES; stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) stat=%d", buf[0], stat);
*/
    // Single sweep and wait
    stat = viPrintf(instr, (ViString)"OPC?;SING;\n");
    qDebug("OPC?;SING stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // Select internal binary format
    stat = viPrintf(instr, (ViString)"FORM1;\n");
    qDebug("FORM1; stat=%d", stat);
    // Output error corrected data
    stat = viPrintf(instr, (ViString)"OUTPDATA;\n");
    qDebug("OUTPDATA; stat=%d", stat);

    // Read in the data header two characters and two bytes for length
    // Read header as 2 byte string
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() hdr 2bytes=\"%s\"(expected \"#A\") stat=%d", buf, stat);
    // Read length as 2 bytes integer
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    datalen = (buf[0] << 8) + buf[1]; /* Big Endian Format */
    qDebug("viRead() length 2bytes=0x%02X 0x%02X=>datalen=%d retCount=%d stat=%d", buf[0], buf[1], datalen, retCount, stat);

    // Read trace data
    qDebug("viRead() all trace data (max size=%d)", sizeof(buf));
 /*
    stat = viRead(instr, buf, sizeof(buf), &retCount);
    qDebug("viRead() stat=%d retCount=%d", stat, retCount);
*/
    stat = viReadToFile(instr, (ViConstString)form1_capture_filename, sizeof(buf), &retCount);
    qDebug("viReadToFile('%s') stat=%d retCount=%d", form1_capture_filename, stat, retCount);

    if(retCount > 0)
    {
        sprintf(debug_info, "HP8753D FORM1 Captured to file %s size=%lu", form1_capture_filename, retCount);
        this->ui->plainTextEdit->appendPlainText(debug_info);
    }else {
        this->ui->plainTextEdit->appendPlainText("HP8753D FORM1 capture error");
    }
    //***********************
    //********* FORM5 *******
    // Select PC_FLOAT32 binary format
    stat = viPrintf(instr, (ViString)"FORM5;\n");
    qDebug("FORM5; stat=%d", stat);
    // Output error corrected data
    stat = viPrintf(instr, (ViString)"OUTPDATA;\n");
    qDebug("OUTPDATA; stat=%d", stat);

    // Read in the data header two characters and two bytes for length
    // Read header as 2 byte string
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() hdr 2bytes=\"%s\"(expected \"#A\") stat=%d", buf, stat);
    // Read length as 2 bytes integer
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    datalen = (buf[1] << 8) + buf[0]; /* Little Endian Format */
    qDebug("viRead() length 2bytes=0x%02X 0x%02X=>datalen=%d retCount=%d stat=%d", buf[0], buf[1], datalen, retCount, stat);

    // Read trace data
    qDebug("viRead() all trace data (max size=%d)", sizeof(buf));
 /*
    stat = viRead(instr, buf, sizeof(buf), &retCount);
    qDebug("viRead() stat=%d retCount=%d", stat, retCount);
*/
    stat = viReadToFile(instr, (ViConstString)form5_capture_filename, sizeof(buf), &retCount);
    qDebug("viReadToFile('%s') stat=%d retCount=%d", form5_capture_filename, stat, retCount);

    if(retCount > 0)
    {
        sprintf(debug_info, "HP8753D FORM5 Captured to file %s size=%lu", form5_capture_filename, retCount);
        this->ui->plainTextEdit->appendPlainText(debug_info);
    }else {
        this->ui->plainTextEdit->appendPlainText("HP8753D FORM5 capture error");
    }

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);
}

void MainWindow::on_pushButtonFORM4_clicked()
{
    // open resource manager
    ViSession rscmng;
    char debug_info[1024];
    // char idn_buf[256] = { 0 };
    static ViByte buf[262144] = { 0 };
    ViUInt32 retCount;
    ViStatus stat;
    char form4_capture_filename[] = { "vna_form4_data.txt" };

    qDebug () << "on_pushButtonFORM4_clicked";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    instrument_setup(instr);

    // Preset the analyzer and wait
/*
    stat = viPrintf(instr, (ViString)"OPC?;PRES;\n");
    qDebug("OPC?;PRES; stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) stat=%d", buf[0], stat);
*/
 /*
    // Set trace length to 201 points
    stat = viPrintf(instr, (ViString)"POIN 201;\n");
    qDebug("POIN 201; stat=%d", stat);
    // Set Start frequency 50 MHz
    stat = viPrintf(instr, (ViString)"STAR 50.E+6;\n");
    qDebug("STAR 50.E+6; stat=%d", stat);
    // Set Stop frequency 200 MHz
    stat = viPrintf(instr, (ViString)"STOP 200.E+6;\n");
    qDebug("STOP 200.E+6; stat=%d", stat);
    // Set log frequency sweep
    stat = viPrintf(instr, (ViString)"LOGFREQ;\n");
    qDebug("LOGFREQ; stat=%d", stat);
*/
    // Single sweep and wait
    stat = viPrintf(instr, (ViString)"OPC?;SING;\n");
    qDebug("OPC?;SING stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // Select form 4 ASCII format
    stat = viPrintf(instr, (ViString)"FORM4;\n");
    qDebug("FORM4; stat=%d", stat);
    // Send formatted trace to controller
    stat = viPrintf(instr, (ViString)"OUTPFORF;\n");
    qDebug("OUTPFORM; stat=%d", stat);

    // Read trace data
    qDebug("viRead() all trace data (max size=%d)", sizeof(buf));
 /*
    stat = viRead(instr, buf, sizeof(buf), &retCount);
    qDebug("viRead() stat=%d retCount=%d", stat, retCount);
*/
    stat = viReadToFile(instr, (ViConstString)form4_capture_filename, sizeof(buf), &retCount);
    qDebug("viReadToFile('%s') stat=%d retCount=%d", form4_capture_filename, stat, retCount);

    if(retCount > 0)
    {
        sprintf(debug_info, "HP8753D FORM4 Captured to file %s size=%lu", form4_capture_filename, retCount);
        this->ui->plainTextEdit->appendPlainText(debug_info);
    }else {
        this->ui->plainTextEdit->appendPlainText("HP8753D FORM4 capture error");
    }
    // Now to calculate the frequency increments between points
    // Read number of points in the trace
    qDebug("POIN?;");
    stat = viPrintf(instr, (ViString)"POIN?;\n");
    // Read Nb Points
    viScanf(instr,(ViString)"%t",&buf);
    qDebug("viScanf() Num_points=%s retCount=%d stat=%d", buf, stat);

    // Read the start frequency
    qDebug("STAR?;");
    stat = viPrintf(instr, (ViString)"STAR?;\n");
    // Read start frequency
    viScanf(instr,(ViString)"%t",&buf);
    qDebug("viScanf() Startf=%s retCount=%d stat=%d", buf, stat);

 /*
    // Read the span & Set SPAN too !!
    qDebug("SPAN?;");
    stat = viPrintf(instr, (ViString)"SPAN?;\n");
    // Read the span
    viScanf(instr,(ViString)"%t",&buf);
    qDebug("viScanf() Span=%s retCount=%d stat=%d", buf, stat);
*/
    // F_inc=Span/(Num_points-1) ! Calculate fixed frequency increment
    // "Point","Freq (MHz)"," Value 1"," Value 2"
    /*
     410 FOR I=1 TO Num_points ! Loop through data points
     420 Freq=Startf+(I-1)*F_inc ! Calculate frequency of data point
     430 PRINT USING 390;I,Freq/1.E+6,Dat(I,1),Dat(I,2)! Print analyzer data
     440 NEXT I
    */
/*
    450 !
    460 OUTPUT @Nwa;"MARKDISC;" ! Discrete marker mode
    470 OUTPUT @Nwa;"MARK1 .3E+6;" ! Position marker at 30 KHz
    480 !
    490 OUTPUT @Nwa;"OPC?;WAIT;" ! Wait for the analyzer to finish
    500 ENTER @Nwa;Reply ! Read the 1 when complete
    510 LOCAL 7 ! Release HP-IB control
    520 !
    530 PRINT
    540 PRINT "Position the marker with the knob and compare the values"
    550 !
    560 END
*/

    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);
}

void MainWindow::on_pushButtonFORM5_clicked()
{
    // open resource manager
    ViSession rscmng;
    char debug_info[1024];
    // char idn_buf[256] = { 0 };
    ViByte buf[65536] = { 0 };
    ViUInt32 retCount;
    ViStatus stat;
    int datalen;
    char form5_capture_filename[] = { "vna_form5_PC_FLOAT32.bin" };

    qDebug () << "on_pushButtonFORM5_clicked";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 10 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 10000);
    /* Clear the device */
    viClear(instr);

    instrument_setup(instr);

    // Preset the analyzer and wait
/*
    stat = viPrintf(instr, (ViString)"OPC?;PRES;\n");
    qDebug("OPC?;PRES; stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) stat=%d", buf[0], stat);
*/
    // Single sweep and wait
    stat = viPrintf(instr, (ViString)"OPC?;SING;\n");
    qDebug("OPC?;SING stat=%d", stat);
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // Select PC_FLOAT32 binary format
    stat = viPrintf(instr, (ViString)"FORM5;\n");
    qDebug("FORM5; stat=%d", stat);
    // Output error corrected data
    stat = viPrintf(instr, (ViString)"OUTPDATA;\n");
    qDebug("OUTPDATA; stat=%d", stat);

    // Read in the data header two characters and two bytes for length
    // Read header as 2 byte string
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() hdr 2bytes=\"%s\"(expected \"#A\") stat=%d", buf, stat);
    // Read length as 2 bytes integer
    memset(buf, 0, 3);
    stat = viRead(instr, buf, 2, &retCount);
    datalen = (buf[1] << 8) + buf[0]; /* Little Endian Format */
    qDebug("viRead() length 2bytes=0x%02X 0x%02X=>datalen=%d retCount=%d stat=%d", buf[0], buf[1], datalen, retCount, stat);

    // Read trace data
    qDebug("viRead() all trace data (max size=%d)", sizeof(buf));
 /*
    stat = viRead(instr, buf, sizeof(buf), &retCount);
    qDebug("viRead() stat=%d retCount=%d", stat, retCount);
*/
    stat = viReadToFile(instr, (ViConstString)form5_capture_filename, sizeof(buf), &retCount);
    qDebug("viReadToFile('%s') stat=%d retCount=%d", form5_capture_filename, stat, retCount);

    if(retCount > 0)
    {
        sprintf(debug_info, "HP8753D FORM5 Captured to file %s size=%lu", form5_capture_filename, retCount);
        this->ui->plainTextEdit->appendPlainText(debug_info);
    }else {
        this->ui->plainTextEdit->appendPlainText("HP8753D FORM5 capture error");
    }
    // Restore continuous sweep
    qDebug("CONT;OPC?;WAIT;");
    stat = viPrintf(instr, (ViString)"CONT;\n");
    // Wait for the analyzer to finish
    stat = viPrintf(instr, (ViString)"OPC?;WAIT;\n");
    // Read the 1 when complete
    memset(buf, 0, 2);
    stat = viRead(instr, buf, 2, &retCount);
    qDebug("viRead() completed=\"%c\" (expected 1) retCount=%d stat=%d", buf[0], retCount, stat);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);
}

void MainWindow::on_pushButton_STIMULUS_READ_clicked()
{
    ViSession rscmng;
    ViStatus stat;

    DOUBLE start_Hz = 0.0;
    DOUBLE stop_Hz = 0.0;

    DOUBLE center_Hz = 0.0;
    DOUBLE span_Hz = 0.0;

    int nb_points = 0;

    DOUBLE step_MHz = 0.0;

    qDebug () << "on_pushButton_STIMULUS_READ_clicked Enter";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 2 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 2000);
    /* Clear the device */
    viClear(instr);

    stat = viPrintf(instr, (ViString)"FORM4;\n");
    qDebug("viPrintf(\"FORM4;\") stat=%d", stat);

    // CENT/SPAN queries
    stat = viPrintf(instr, (ViString)"CENT;OUTPACTI;\n");
    qDebug("viPrintf(\"CENT;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &center_Hz);
    qDebug("viScanf() center_Hz=%lf stat=%d", center_Hz, stat);
    this->ui->doubleSpinBox_Center->setValue( (center_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"SPAN;OUTPACTI;\n");
    qDebug("viPrintf(\"SPAN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &span_Hz);
    qDebug("viScanf() span_Hz=%lf stat=%d", span_Hz, stat);
    this->ui->doubleSpinBox_Span->setValue( (span_Hz/MHZ_VAL) );

    /*
    // Compute Step using Span Frequency
    step_MHz = (span_Hz / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);
    */

    // STAR/STOP queries
    stat = viPrintf(instr, (ViString)"STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);
    this->ui->doubleSpinBox_Start->setValue( (start_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);
    this->ui->doubleSpinBox_Stop->setValue( (stop_Hz/MHZ_VAL) );

    // (POIN query)
    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%d", &nb_points);
    qDebug("viScanf() fn=%d stat=%d", nb_points, stat);
    this->ui->spinBox_NbPoints->setValue(nb_points);

    // Compute Step using Start/Stop Frequency
    step_MHz = ((stop_Hz - start_Hz) / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);

    // c1lose VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButton_STIMULUS_READ_clicked Exit";
}

void MainWindow::on_pushButton_START_STOP_WRITE_clicked()
{
    ViSession rscmng;
    ViStatus stat;

    DOUBLE start_MHz = 0.0;
    DOUBLE start_Hz = 0.0;

    DOUBLE stop_MHz = 0.0;
    DOUBLE stop_Hz = 0.0;

    DOUBLE center_Hz = 0.0;
    DOUBLE span_Hz = 0.0;

    DOUBLE step_MHz = 0.0;
    int nb_points = 0;

    qDebug () << "on_pushButton_START_STOP_WRITE_clicked Enter";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 2 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 2000);
    /* Clear the device */
    viClear(instr);

    stat = viPrintf(instr, (ViString)"FORM4;\n");
    qDebug("viPrintf(\"FORM4;\") stat=%d", stat);

    /* Write STAR */
    start_MHz = this->ui->doubleSpinBox_Start->value();
    start_Hz = start_MHz * MHZ_VAL;
    // Set Start frequency
    stat = viPrintf(instr, (ViString)"STAR %lf;\n", start_Hz);
    qDebug("STAR %lf; stat=%d", start_Hz, stat);

    /* Write STOP */
    stop_MHz = this->ui->doubleSpinBox_Stop->value();
    stop_Hz = stop_MHz * MHZ_VAL;
    // Set Stop frequency
    stat = viPrintf(instr, (ViString)"STOP %lf;\n", stop_Hz);
    qDebug("STOP %lf; stat=%d", stop_Hz, stat);

    /* Write NB POINTS */
    nb_points = this->ui->spinBox_NbPoints->value();
    // Set trace length to nb_points
    stat = viPrintf(instr, (ViString)"POIN %lf;\n", (DOUBLE)nb_points);
    qDebug("POIN %d; stat=%d", nb_points, stat);

    /* Read back CENT/SPAN/STAR/STOP/POIN */

    // CENT/SPAN queries
    stat = viPrintf(instr, (ViString)"CENT;OUTPACTI;\n");
    qDebug("viPrintf(\"CENT;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &center_Hz);
    qDebug("viScanf() center_Hz=%lf stat=%d", center_Hz, stat);
    this->ui->doubleSpinBox_Center->setValue( (center_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"SPAN;OUTPACTI;\n");
    qDebug("viPrintf(\"SPAN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &span_Hz);
    qDebug("viScanf() span_Hz=%lf stat=%d", span_Hz, stat);
    this->ui->doubleSpinBox_Span->setValue( (span_Hz/MHZ_VAL) );
    /*
    // Compute Step using Span Frequency
    step_MHz = (span_Hz / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);
    */

    // STAR/STOP/POIN queries
    stat = viPrintf(instr, (ViString)"STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);
    this->ui->doubleSpinBox_Start->setValue( (start_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);
    this->ui->doubleSpinBox_Stop->setValue( (stop_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%d", &nb_points);
    qDebug("viScanf() fn=%d stat=%d", nb_points, stat);
    this->ui->spinBox_NbPoints->setValue(nb_points);

    step_MHz = ((stop_Hz - start_Hz) / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButton_START_STOP_WRITE_clicked Exit";
}

void MainWindow::on_pushButton_CENTER_SPAN_WRITE_clicked()
{
    ViSession rscmng;
    ViStatus stat;

    DOUBLE start_Hz = 0.0;
    DOUBLE stop_Hz = 0.0;

    DOUBLE center_MHz = 0.0;
    DOUBLE center_Hz = 0.0;

    DOUBLE span_MHz = 0.0;
    DOUBLE span_Hz = 0.0;

    DOUBLE step_MHz = 0.0;
    int nb_points = 0;

    qDebug () << "on_pushButton_CENTER_SPAN_WRITE_clicked Enter";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 2 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 2000);
    /* Clear the device */
    viClear(instr);

    stat = viPrintf(instr, (ViString)"FORM4;\n");
    qDebug("viPrintf(\"FORM4;\") stat=%d", stat);

    /* Write CENT */
    center_MHz = this->ui->doubleSpinBox_Center->value();
    center_Hz = center_MHz * MHZ_VAL;
    // Set Center
    stat = viPrintf(instr, (ViString)"CENT %lf;\n", center_Hz);
    qDebug("CENT %lf; stat=%d", center_Hz, stat);

    /* Write SPAN */
    span_MHz = this->ui->doubleSpinBox_Span->value();
    span_Hz = span_MHz * MHZ_VAL;
    // Set Center
    stat = viPrintf(instr, (ViString)"SPAN %lf;\n", span_Hz);
    qDebug("SPAN %lf; stat=%d", span_Hz, stat);

    /* Read back CENT/SPAN/STAR/STOP/POIN */

    // CENT/SPAN queries
    stat = viPrintf(instr, (ViString)"CENT;OUTPACTI;\n");
    qDebug("viPrintf(\"CENT;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &center_Hz);
    qDebug("viScanf() center_Hz=%lf stat=%d", center_Hz, stat);
    this->ui->doubleSpinBox_Center->setValue( (center_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"SPAN;OUTPACTI;\n");
    qDebug("viPrintf(\"SPAN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &span_Hz);
    qDebug("viScanf() span_Hz=%lf stat=%d", span_Hz, stat);
    this->ui->doubleSpinBox_Span->setValue( (span_Hz/MHZ_VAL) );
    /*
    // Compute Step using Span Frequency
    step_MHz = (span_Hz / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);
    */

    // STAR/STOP/POIN queries
    stat = viPrintf(instr, (ViString)"STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);
    this->ui->doubleSpinBox_Start->setValue( (start_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);
    this->ui->doubleSpinBox_Stop->setValue( (stop_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%d", &nb_points);
    qDebug("viScanf() fn=%d stat=%d", nb_points, stat);
    this->ui->spinBox_NbPoints->setValue(nb_points);

    step_MHz = ((stop_Hz - start_Hz) / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButton_CENTER_SPAN_WRITE_clicked Exit";
}

void MainWindow::on_pushButton_NB_POINTS_WRITE_clicked()
{
    ViSession rscmng;
    ViStatus stat;

    DOUBLE start_Hz = 0.0;
    DOUBLE stop_Hz = 0.0;
    DOUBLE center_Hz = 0.0;
    DOUBLE span_Hz = 0.0;

    DOUBLE step_MHz = 0.0;
    int nb_points = 0;

    qDebug () << "on_pushButton_NB_POINTS_WRITE_clicked Enter";

    stat = viOpenDefaultRM(&rscmng);
    if (stat < VI_SUCCESS)
    {
       qDebug () << "Could not open a session to the VISA Resource Manager!\n";
       exit (EXIT_FAILURE);
    }
    qDebug("viOpenDefaultRM stat=0x%08X stat=%d", rscmng, stat);

    // connect to the VNA
    static ViSession instr;
    stat = viOpen(rscmng, (ViRsrc) VISA_GPIB_RES_STR, VI_NULL, VI_NULL, &instr);
    if (stat < VI_SUCCESS)
    {
       qDebug("viOpen stat=%d", stat);
       QString info = QString("Could not open resource ") + QString(VISA_GPIB_RES_STR);
       qDebug() << info;
       this->ui->plainTextEdit->appendPlainText(info);
       return;
    }
    qDebug("viOpen stat=%d", stat);
    /* Initialize the timeout attribute to 2 s */
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 2000);
    /* Clear the device */
    viClear(instr);

    stat = viPrintf(instr, (ViString)"FORM4;\n");
    qDebug("viPrintf(\"FORM4;\") stat=%d", stat);

    /* Write NB POINTS */
    nb_points = this->ui->spinBox_NbPoints->value();
    // Set trace length to nb_points
    stat = viPrintf(instr, (ViString)"POIN %lf;\n", (DOUBLE)nb_points);
    qDebug("POIN %d; stat=%d", nb_points, stat);

    /* Read back CENT/SPAN/STAR/STOP/POIN */

    // CENT/SPAN queries
    stat = viPrintf(instr, (ViString)"CENT;OUTPACTI;\n");
    qDebug("viPrintf(\"CENT;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &center_Hz);
    qDebug("viScanf() center_Hz=%lf stat=%d", center_Hz, stat);
    this->ui->doubleSpinBox_Center->setValue( (center_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"SPAN;OUTPACTI;\n");
    qDebug("viPrintf(\"SPAN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &span_Hz);
    qDebug("viScanf() span_Hz=%lf stat=%d", span_Hz, stat);
    this->ui->doubleSpinBox_Span->setValue( (span_Hz/MHZ_VAL) );
    /*
    // Compute Step using Span Frequency
    step_MHz = (span_Hz / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);
    */

    // STAR/STOP/POIN queries
    stat = viPrintf(instr, (ViString)"STAR;OUTPACTI;\n");
    qDebug("viPrintf(\"FORM4;STAR;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &start_Hz);
    qDebug("viScanf() start_Hz=%lf stat=%d", start_Hz, stat);
    this->ui->doubleSpinBox_Start->setValue( (start_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"STOP;OUTPACTI;\n");
    qDebug("viPrintf(\"STOP;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%lf", &stop_Hz);
    qDebug("viScanf() stop_Hz=%lf stat=%d", stop_Hz, stat);
    this->ui->doubleSpinBox_Stop->setValue( (stop_Hz/MHZ_VAL) );

    stat = viPrintf(instr, (ViString)"POIN;OUTPACTI;\n");
    qDebug("viPrintf(\"POIN;OUTPACTI;\") stat=%d", stat);
    stat = viScanf(instr,(ViString)"%d", &nb_points);
    qDebug("viScanf() fn=%d stat=%d", nb_points, stat);
    this->ui->spinBox_NbPoints->setValue(nb_points);

    step_MHz = ((stop_Hz - start_Hz) / (double)(nb_points-1)) / (double)MHZ_VAL;
    qDebug("step_MHz=%lf()", step_MHz);
    this->ui->doubleSpinBox_Step->setValue(step_MHz);

    // close VI sessions
    viClose(instr);
    viClose(rscmng);

    qDebug () << "on_pushButton_NB_POINTS_WRITE_clicked Exit";
}

void MainWindow::on_pushButton_OpenCaptureDir_clicked()
{
    if(this->savefile_path.length() == 0)
    {
        this->savefile_path = "./";
    }

    QString path = QDir::toNativeSeparators(this->savefile_path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
