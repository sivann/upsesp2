#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include <time.h>

char version[]="0.6, 10-Apr-2010";
/*
  Spiros Ioannou, sivann xat gmail.com 2009


  v0.2: compiles with -pedantic, fixes some segfaults.
  v0.3: more debug output
  v0.4: fixed negative values, BATTERY_CURRENT divider
  v0.5: fixed MSByte  (byteH) status detection, added support for bit-rage values (INPUT_HOT_COUNT)
  v0.6: getopt, added lots of measurement parameters, support for float divisors/multipliers

  based on the work of R.Gregory (http://www.csc.liv.ac.uk/~greg/projects/liebertserial/)
  Compile with: gcc -O2 upsesp2.c -o upsesp2
  Run with: ./upsesp2 /dev/ttyS0 Replace ttyS0 with your serial port (ttyS1, ...etc).

  TODO: support UPS commands (shutdown, restart-after x seconds, etc). More sniffing needed.

 */

//#define BAUDRATE B2400
#define BAUDRATE B9600

/***********************************************************************************************/

#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define NCMD 500 /*max number of commands*/
#define MAXAGE 5 /*max seconds to accept data as current*/
#define IsBitSet(val, bit) ((val) & (1 << (bit)))

/* port stuff */
int fd;
int debug;
struct termios oldtio,newtio;
char str[256];

/***************************************************/
typedef struct
{
  char descr[120];
  unsigned char type;   /*0:measurement, 1:ascii, 2:status*/
  unsigned char length; /*for ascii, how many cmds*/
  float rfmt;   /*unit divider for measurement, bit position for status*/
  unsigned char cmd[6]; /*UPS command bytes*/
  unsigned char rbyteH; /*H byte result*/
  unsigned char rbyteL; /*L byte result*/
  char rstr[256]; /*string result from multiple commands (lenght>1)*/
  time_t when; /*time of last result*/
  unsigned char init;   /*used to check if struct block has something valid*/
  unsigned char supported;   /*if this feature is supported*/
} cmd_s;

/*fill struct*/
/*fs(0,&c[0],"lala1", 1,2,3,4,5,6, 11,22,33,44,55,66);*/
void fs( cmd_s * c, 
    char * descr, 
    unsigned char type,
    unsigned char length,
    float rfmt,
    unsigned int c0, unsigned int c1, 
    unsigned int c2, unsigned int c3,
    unsigned int c4, unsigned int c5)
{
  snprintf(c->descr,120,"%s",descr);
  c->cmd[0]=c0;c->cmd[1]=c1;c->cmd[2]=c2;c->cmd[3]=c3;c->cmd[4]=c4;c->cmd[5]=c5;
  c->rstr[0]=0;
  c->type=type;
  c->when=0;
  c->rfmt=rfmt;
  c->length=length;
  c->init=1;
  c->supported=1; /*supported until proven otherwise (UPS doesn't repond) */
}

/* Initialize cmd_s structure with UPS commands and expected reply format */
void  initcmd(cmd_s *c)
{
  /*Format:*/
  /*STRINGID, type,length,rfmt,   cmd*/

  /*type:   0:measurement, 1:ascii, 2:status */
  /*length: string length for ascii info*/
  /*rmft:   unit divider (measurement) OR bit position (status) */

  /*cmd: UPS commands. Checksums (last byte) will be recalculated on write */


  fs(c++,"DISCHARGE_COUNT", 0,0,1, 1,150,2,1,1 ,000);
  fs(c++,"CYCLE_DURATION", 0,0,1, 1,150,2,1,2 ,000);
  fs(c++,"AMP_HOURS_DRAWN", 0,0,1, 1,150,2,1,3 ,000);
  fs(c++,"KILOWATT_HOURS_DRAWN", 0,0,1, 1,150,2,1,4 ,000);
  fs(c++,"WATT_HOURS_DRAWN", 0,0,1, 1,150,2,1,5 ,000);
  fs(c++,"RESET", 0,0,1, 1,150,2,1,6 ,000);

  fs(c++,"PHASE0_INPUT_VOLTAGE_LX_RMS", 0,0,10, 1,144,2,1,1 ,000);
  fs(c++,"PHASE0_INPUT_CURRENT_RMS", 0,0,10, 1,144,2,1,2 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LX_RMS", 0,0,10, 1,144,2,1,3 ,000);
  fs(c++,"PHASE0_OUTPUT_CURRENT_RMS", 0,0,10, 1,144,2,1,4 ,000);
  fs(c++,"PHASE0_BYPASS_VOLTAGE_RMS", 0,0,10, 1,144,2,1,5 ,000);
  fs(c++,"PHASE0_BYPASS_CURRENT_RMS", 0,0,10, 1,144,2,1,6 ,000);
  fs(c++,"PHASE0_INPUT_VOLTAGE_LL_MAX", 0,0,10, 1,144,2,1,7 ,000);
  fs(c++,"PHASE0_INPUT_VOLTAGE_LL_MIN", 0,0,10, 1,144,2,1,8 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LL_MAX", 0,0,10, 1,144,2,1,9 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LL_MIN", 0,0,10, 1,144,2,1,10 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LN_RMS", 0,0,10, 1,144,2,1,11 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LL_THD", 0,0,10, 1,144,2,1,12 ,000);
  fs(c++,"PHASE0_OUTPUT_CURRENT_THD", 0,0,10, 1,144,2,1,13 ,000);
  fs(c++,"PHASE0_OUTPUT_CURRENT_CREST_FACTOR", 0,0,10, 1,144,2,1,14 ,000);
  fs(c++,"PHASE0_OUTPUT_CURRENT_K_FACTOR", 0,0,10, 1,144,2,1,15 ,000);
  fs(c++,"PHASE0_OUTPUT_VOLTAGE_LL_RMS", 0,0,10, 1,144,2,1,16 ,000);
  fs(c++,"PHASE0_INPUT_VOLTAGE_LN_RMS", 0,0,10, 1,144,2,1,17 ,000);
  fs(c++,"PHASE0_INPUT_VOLTAGE_LL_RMS", 0,0,10, 1,144,2,1,18 ,000);
  fs(c++,"PHASE0_BYPASS_VOLTAGE_LN_RMS", 0,0,10, 1,144,2,1,19 ,000);
  fs(c++,"PHASE0_BYPASS_VOLTAGE_LL_RMS", 0,0,10, 1,144,2,1,20 ,000);

  fs(c++,"PHASE0_OUT_WATT_PH_A", 0,0,0.01, 1,144,2,1,21 ,000);
  fs(c++,"PHASE0_OUT_VA_PH_A", 0,0,0.01, 1,144,2,1,22 ,000);
  fs(c++,"PHASE0_OUT_VAR_PH_A", 0,0,0.01, 1,144,2,1,23 ,000);

  fs(c++,"PHASE0_OUT_PERCENT_LOAD_PH_A", 0,0,1, 1,144,2,1,24 ,000);

  fs(c++,"PHASE0_OUT_POWER_FACTOR_PH_A", 0,0,100, 1,144,2,1,25 ,000);
  fs(c++,"PHASE0_OUT_POWER_FACTOR_LDLG_PH_A", 0,0,100, 1,144,2,1,26 ,000);
  fs(c++,"PHASE0_INP_POWER_FACTOR_PH_A", 0,0,100, 1,144,2,1,27 ,000);
  fs(c++,"PHASE0_INP_POWER_FACTOR_LDLG_PH_A", 0,0,100, 1,144,2,1,28 ,000);

  fs(c++,"PHASE1_INPUT_VOLTAGE_LX_RMS", 0,0,10, 1,145,2,1,1 ,000);
  fs(c++,"PHASE1_INPUT_CURRENT_RMS", 0,0,10, 1,145,2,1,2 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LX_RMS", 0,0,10, 1,145,2,1,3 ,000);
  fs(c++,"PHASE1_OUTPUT_CURRENT_RMS", 0,0,10, 1,145,2,1,4 ,000);
  fs(c++,"PHASE1_BYPASS_VOLTAGE_RMS", 0,0,10, 1,145,2,1,5 ,000);
  fs(c++,"PHASE1_BYPASS_CURRENT_RMS", 0,0,10, 1,145,2,1,6 ,000);
  fs(c++,"PHASE1_INPUT_VOLTAGE_LL_MAX", 0,0,10, 1,145,2,1,7 ,000);
  fs(c++,"PHASE1_INPUT_VOLTAGE_LL_MIN", 0,0,10, 1,145,2,1,8 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LL_MAX", 0,0,10, 1,145,2,1,9 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LL_MIN", 0,0,10, 1,145,2,1,10 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LN_RMS", 0,0,10, 1,145,2,1,11 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LL_THD", 0,0,10, 1,145,2,1,12 ,000);
  fs(c++,"PHASE1_OUTPUT_CURRENT_THD", 0,0,10, 1,145,2,1,13 ,000);
  fs(c++,"PHASE1_OUTPUT_CURRENT_CREST_FACTOR", 0,0,10, 1,145,2,1,14 ,000);
  fs(c++,"PHASE1_OUTPUT_CURRENT_K_FACTOR", 0,0,10, 1,145,2,1,15 ,000);
  fs(c++,"PHASE1_OUTPUT_VOLTAGE_LL_RMS", 0,0,10, 1,145,2,1,16 ,000);
  fs(c++,"PHASE1_INPUT_VOLTAGE_LN_RMS", 0,0,10, 1,145,2,1,17 ,000);
  fs(c++,"PHASE1_INPUT_VOLTAGE_LL_RMS", 0,0,10, 1,145,2,1,18 ,000);
  fs(c++,"PHASE1_BYPASS_VOLTAGE_LN_RMS", 0,0,10, 1,145,2,1,19 ,000);
  fs(c++,"PHASE1_BYPASS_VOLTAGE_LL_RMS", 0,0,10, 1,145,2,1,20 ,000);

  fs(c++,"PHASE1_OUT_WATT_PH_B", 0,0,0.01, 1,145,2,1,21 ,000);
  fs(c++,"PHASE1_OUT_VA_PH_B", 0,0,0.01, 1,145,2,1,22 ,000);
  fs(c++,"PHASE1_OUT_VAR_PH_B", 0,0,0.01, 1,145,2,1,23 ,000);

  fs(c++,"PHASE1_OUT_PERCENT_LOAD_PH_B", 0,0,1, 1,145,2,1,24 ,000);

  fs(c++,"PHASE1_OUT_POWER_FACTOR_PH_B", 0,0,100, 1,145,2,1,25 ,000);
  fs(c++,"PHASE1_OUT_POWER_FACTOR_LDLG_PH_B", 0,0,100, 1,145,2,1,26 ,000);
  fs(c++,"PHASE1_INP_POWER_FACTOR_PH_B", 0,0,100, 1,145,2,1,27 ,000);
  fs(c++,"PHASE1_INP_POWER_FACTOR_LDLG_PH_B", 0,0,100, 1,145,2,1,28 ,000);

  fs(c++,"PHASE2_INPUT_VOLTAGE_LX_RMS", 0,0,10, 1,146,2,1,1 ,000);
  fs(c++,"PHASE2_INPUT_CURRENT_RMS", 0,0,10, 1,146,2,1,2 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LX_RMS", 0,0,10, 1,146,2,1,3 ,000);
  fs(c++,"PHASE2_OUTPUT_CURRENT_RMS", 0,0,10, 1,146,2,1,4 ,000);
  fs(c++,"PHASE2_BYPASS_VOLTAGE_RMS", 0,0,10, 1,146,2,1,5 ,000);
  fs(c++,"PHASE2_BYPASS_CURRENT_RMS", 0,0,10, 1,146,2,1,6 ,000);
  fs(c++,"PHASE2_INPUT_VOLTAGE_LL_MAX", 0,0,10, 1,146,2,1,7 ,000);
  fs(c++,"PHASE2_INPUT_VOLTAGE_LL_MIN", 0,0,10, 1,146,2,1,8 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LL_MAX", 0,0,10, 1,146,2,1,9 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LL_MIN", 0,0,10, 1,146,2,1,10 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LN_RMS", 0,0,10, 1,146,2,1,11 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LL_THD", 0,0,10, 1,146,2,1,12 ,000);
  fs(c++,"PHASE2_OUTPUT_CURRENT_THD", 0,0,10, 1,146,2,1,13 ,000);
  fs(c++,"PHASE2_OUTPUT_CURRENT_CREST_FACTOR", 0,0,10, 1,146,2,1,14 ,000);
  fs(c++,"PHASE2_OUTPUT_CURRENT_K_FACTOR", 0,0,10, 1,146,2,1,15 ,000);
  fs(c++,"PHASE2_OUTPUT_VOLTAGE_LL_RMS", 0,0,10, 1,146,2,1,16 ,000);
  fs(c++,"PHASE2_INPUT_VOLTAGE_LN_RMS", 0,0,10, 1,146,2,1,17 ,000);
  fs(c++,"PHASE2_INPUT_VOLTAGE_LL_RMS", 0,0,10, 1,146,2,1,18 ,000);
  fs(c++,"PHASE2_BYPASS_VOLTAGE_LN_RMS", 0,0,10, 1,146,2,1,19 ,000);
  fs(c++,"PHASE2_BYPASS_VOLTAGE_LL_RMS", 0,0,10, 1,146,2,1,20 ,000);

  fs(c++,"PHASE2_OUT_WATT_PH_C", 0,0,0.01, 1,146,2,1,21 ,000);
  fs(c++,"PHASE2_OUT_VA_PH_C", 0,0,0.01, 1,146,2,1,22 ,000);
  fs(c++,"PHASE2_OUT_VAR_PH_C", 0,0,0.01, 1,146,2,1,23 ,000);

  fs(c++,"PHASE2_OUT_PERCENT_LOAD_PH_C", 0,0,1, 1,146,2,1,24 ,000);

  fs(c++,"PHASE2_OUT_POWER_FACTOR_PH_C", 0,0,100, 1,146,2,1,25 ,000);
  fs(c++,"PHASE2_OUT_POWER_FACTOR_LDLG_PH_C", 0,0,100, 1,146,2,1,26 ,000);
  fs(c++,"PHASE2_INP_POWER_FACTOR_PH_C", 0,0,100, 1,146,2,1,27 ,000);
  fs(c++,"PHASE2_INP_POWER_FACTOR_LDLG_PH_C", 0,0,100, 1,146,2,1,28 ,000);

  fs(c++,"LOCATION", 1,15,0, 1,161,2,1,19 ,000);
  fs(c++,"CONTACT", 1,8,0, 1,133,2,1,3 ,000);
  fs(c++,"PHONE_NUMBER", 1,10,0, 1,133,2,1,11 ,000);
  fs(c++,"MODEL_NAME", 1,15,0, 1,136,2,1,4 ,000);
  fs(c++,"FIRMWARE_REV", 1,8,0, 1,136,2,1,19 ,000);
  fs(c++,"SERIAL_NUMBER", 1,10,0, 1,136,2,1,27 ,000);
  fs(c++,"MFG_DATE", 1,4,0, 1,136,2,1,37 ,000);

  fs(c++,"INPUT_HOT_COUNT", 2,2,0, 1,136,2,1,1 ,000);
  fs(c++,"INPUT_SPLIT" ,2,1,2, 1,136,2,1,1 ,000);

  fs(c++,"OUTPUT_HOT_COUNT", 2,2,4, 1,136,2,1,1 ,000);
  fs(c++,"OUTPUT_SPLIT" ,2,1,6, 1,136,2,1,1 ,000);
  fs(c++,"BYPASS_HOT_COUNT", 2,2,8, 1,136,2,1,1 ,000);
  fs(c++,"BYPASS_SPLIT" ,2,1,10, 1,136,2,1,1 ,000);
  fs(c++,"OFFLINE_UPS" ,2,1,12, 1,136,2,1,1 ,000);
  fs(c++,"INTERACTIVE_UPS" ,2,1,13, 1,136,2,1,1 ,000);
  fs(c++,"DUAL_INPUT_UPS" ,2,1,14, 1,136,2,1,1 ,000);
  fs(c++,"SUB_MODULE_COUNT", 2,5,0, 1,136,2,1,2 ,000);
  fs(c++,"OUTLET_MAP", 0,0,1, 1,136,2,1,3 ,000);


  fs(c++,"BATTERY_COUNT", 0,0,1, 1,136,2,1,41 ,000);
  fs(c++,"POWER_MODULE_COUNT", 2,8,0, 1,136,2,1,42 ,000);
  fs(c++,"BATTERY_MODULE_COUNT", 2,8,8, 1,136,2,1,42 ,000);
  fs(c++,"REDUNDANT_CONTROL_MODULE_PRESENT" ,2,1,0, 1,136,2,1,43 ,000);
  fs(c++,"FREQUENCY_CONVERSION" ,2,1,1, 1,136,2,1,43 ,000);
  fs(c++,"VOLTAGE_CONVERSION" ,2,1,2, 1,136,2,1,43 ,000);
  fs(c++,"HAS_INVERTER_TEMP" ,2,1,0, 1,137,2,1,25 ,000);
  fs(c++,"HAS_BATTERY_TEMP" ,2,1,1, 1,137,2,1,25 ,000);
  fs(c++,"HAS_PFC_TEMP" ,2,1,2, 1,137,2,1,25 ,000);
  fs(c++,"HAS_AMBIENT_TEMP" ,2,1,3, 1,137,2,1,25 ,000);
  fs(c++,"HAS_AUX1_TEMP" ,2,1,4, 1,137,2,1,25 ,000);
  fs(c++,"HAS_AUX2_TEMP" ,2,1,5, 1,137,2,1,25 ,000);

  fs(c++,"BATTERY_TIME_REMAIN", 0,0,0.016666666, 1,149,2,1,1 ,000);
  fs(c++,"BATTERY_VOLTAGE", 0,0,10, 1,149,2,1,2 ,000);
  fs(c++,"BATTERY_CURRENT", 0,0,100, 1,149,2,1,3 ,000);
  fs(c++,"BATTERY_CAPACITY", 0,0,1, 1,149,2,1,4 ,000);

  fs(c++,"LOAD_WATTS", 0,0,1, 1,149,2,1,5 ,000);
  fs(c++,"LOAD_VA", 0,0,1, 1,149,2,1,6 ,000);
  fs(c++,"LOAD_PERCENT", 0,0,1, 1,149,2,1,7 ,000);

  fs(c++,"INPUT_FREQUENCY", 0,0,100, 1,149,2,1,8 ,000);
  fs(c++,"OUTPUT_FREQUENCY", 0,0,100, 1,149,2,1,9 ,000);
  fs(c++,"BYPASS_FREQUENCY", 0,0,100, 1,149,2,1,10 ,000);

  fs(c++,"INVERTER_TEMP", 0,0,10, 1,149,2,1,11 ,000);
  fs(c++,"BATTERY_TEMP", 0,0,10, 1,149,2,1,12 ,000);
  fs(c++,"PFC_TEMP", 0,0,10, 1,149,2,1,13 ,000);
  fs(c++,"AMBIENT_TEMP", 0,0,10, 1,149,2,1,14 ,000);

  fs(c++,"AUX_TEMP_1", 0,0,10, 1,149,2,1,15 ,000);
  fs(c++,"AUX_TEMP_2", 0,0,10, 1,149,2,1,16 ,000);
  fs(c++,"XFRMR_TEMP", 0,0,10, 1,149,2,1,17 ,000);

  fs(c++,"INP_POWER_FACTOR", 0,0,100, 1,149,2,1,19 ,000);
  fs(c++,"INP_POWER_FACTOR_LDLG", 0,0,100, 1,149,2,1,20 ,000);
  fs(c++,"REREAD", 0,0,1, 1,161,2,1,1 ,000);
  fs(c++,"CURRENT_MODULE_ID", 0,0,1, 1,161,2,1,2 ,000);
  fs(c++,"AUTO_RESTART", 0,0,1, 1,161,2,1,3 ,000);
  fs(c++,"AUTO_RESTART_DELAY_SECS", 0,0,0.1, 1,161,2,1,4 ,000);

  fs(c++,"NOMINAL_INPUT_VOLTAGE", 0,0,10, 1,161,2,1,5 ,000);
  fs(c++,"NOMINAL_OUTPUT_VOLTAGE", 0,0,10, 1,161,2,1,6 ,000);
  fs(c++,"NOMINAL_BYPASS_VOLTAGE", 0,0,10, 1,161,2,1,7 ,000);
  fs(c++,"NOMINAL_VA", 0,0,0.01, 1,161,2,1,8 ,000);
  fs(c++,"NOMINAL_INPUT_FREQ", 0,0,100, 1,161,2,1,9 ,000);
  fs(c++,"NOMINAL_OUTPUT_FREQ", 0,0,100, 1,161,2,1,10 ,000);
  fs(c++,"NOMINAL_POWER_FACTOR", 0,0,100, 1,161,2,1,11 ,000); /* snmp gives different results */
  fs(c++,"NOMINAL_INPUT_CURRENT", 0,0,10, 1,161,2,1,12 ,000);
  fs(c++,"NOMINAL_BATTERY_VOLTAGE", 0,0,10, 1,161,2,1,13 ,000);

  fs(c++,"LOW_BATTERY_TIME", 0,0,0.0166666, 1,161,2,1,14 ,000); /*output is normaly in minutes */
  fs(c++,"SYSTEM_CAPACITY", 0,0,1, 1,161,2,1,34 ,000);
  fs(c++,"SYSTEM_FRAME_CAPACITY", 0,0,1, 1,161,2,1,35 ,000);
  fs(c++,"AUTO_RESTART_PERCENT", 0,0,1, 1,161,2,1,36 ,000);
  fs(c++,"PARALLEL_MODULE_MODE", 0,0,1, 1,161,2,1,37 ,000);
  fs(c++,"PARALLEL_MODULE_COUNT", 0,0,1, 1,161,2,1,38 ,000);
  fs(c++,"PARALLEL_REDUN_COUNT", 0,0,1, 1,161,2,1,39 ,000);
  fs(c++,"PARALLEL_MODULE_ID", 0,0,1, 1,161,2,1,40 ,000);
  fs(c++,"PARALLEL_LBS_MODE", 0,0,1, 1,161,2,1,41 ,000);
  fs(c++,"PARALLEL_ECO_MODE", 0,0,1, 1,161,2,1,42 ,000);
  fs(c++,"NOM_BAT_CAPACITY", 0,0,1, 1,162,2,1,1 ,000);

  fs(c++,"BAT_FLOAT_VOLTAGE", 0,0,10, 1,162,2,1,2 ,000);
  fs(c++,"NOM_DC1_VOLTAGE", 0,0,10, 1,162,2,1,3 ,000);
  fs(c++,"NOM_DC2_VOLTAGE", 0,0,10, 1,162,2,1,4 ,000);
  fs(c++,"BAT_EOD_VOLTAGE", 0,0,10, 1,162,2,1,5 ,000);

  fs(c++,"BYPASS_LOW_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,6 ,000);
  fs(c++,"BYPASS_LOW_VOLTAGE_WARN", 0,0,1, 1,162,2,1,7 ,000);
  fs(c++,"BYPASS_HIGH_VOLTAGE_WARN", 0,0,1, 1,162,2,1,8 ,000);
  fs(c++,"BYPASS_HIGH_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,9 ,000);
  fs(c++,"OUTPUT_LOW_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,10 ,000);
  fs(c++,"OUTPUT_LOW_VOLTAGE_WARN", 0,0,1, 1,162,2,1,11 ,000);
  fs(c++,"OUTPUT_HIGH_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,12 ,000);
  fs(c++,"OUTPUT_HIGH_VOLTAGE_WARN", 0,0,1, 1,162,2,1,13 ,000);
  fs(c++,"BAT_HIGH_VOLTAGE_WARN", 0,0,1, 1,162,2,1,14 ,000);
  fs(c++,"BAT_HIGH_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,15 ,000);
  fs(c++,"DC1_LOW_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,16 ,000);
  fs(c++,"DC1_LOW_VOLTAGE_WARN", 0,0,1, 1,162,2,1,17 ,000);
  fs(c++,"DC1_HIGH_VOLTAGE_WARN", 0,0,1, 1,162,2,1,18 ,000);
  fs(c++,"DC1_HIGH_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,19 ,000);
  fs(c++,"DC2_LOW_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,20 ,000);
  fs(c++,"DC2_LOW_VOLTAGE_WARN", 0,0,1, 1,162,2,1,21 ,000);
  fs(c++,"DC2_HIGH_VOLTAGE_WARN", 0,0,1, 1,162,2,1,22 ,000);
  fs(c++,"DC2_HIGH_VOLTAGE_FAIL", 0,0,1, 1,162,2,1,23 ,000);
  fs(c++,"AMBIENT_TEMP_WARN", 0,0,1, 1,162,2,1,30 ,000);
  fs(c++,"INV_TEMP_WARN", 0,0,1, 1,162,2,1,31 ,000);
  fs(c++,"BAT_TEMP_WARN", 0,0,1, 1,162,2,1,32 ,000);
  fs(c++,"TEMP_FAIL", 0,0,1, 1,162,2,1,33 ,000);
  fs(c++,"CONVERTER_TEMP_WARN", 0,0,1, 1,162,2,1,34 ,000);
  fs(c++,"OVERTEMP_WARNING" ,2,1,5, 1,148,2,1,1 ,000);

  fs(c++,"AUDIBLE_ALARM_ENABLE", 0,0,1, 1,162,2,1,35 ,000);
  fs(c++,"BAT_AUTO_TEST_ENABLE", 0,0,1, 1,162,2,1,36 ,000);
  fs(c++,"MIN_REDUNDANT_PWR_MODULES", 2,7,0, 1,162,2,1,37 ,000);
  fs(c++,"MIN_REDUNDANT_BAT_MODULES", 2,7,8, 1,162,2,1,37 ,000);
  fs(c++,"STANDBY" ,2,1,15, 1,162,2,1,37 ,000);
  fs(c++,"MAX_LOAD_VA", 0,0,1, 1,162,2,1,38 ,000);
  fs(c++,"BAT_TEST_INTERVAL", 0,0,1, 1,162,2,1,39 ,000);
  fs(c++,"BAT_TEST_START_TIME", 0,0,1, 1,162,2,1,40 ,000);
  fs(c++,"PFC_ON" ,2,1,0, 1,148,2,1,1 ,000);
  fs(c++,"DC_DC_CONVERTER_STATE" ,2,1,1, 1,148,2,1,1 ,000);
  fs(c++,"ON_INVERTER" ,2,1,2, 1,148,2,1,1 ,000);
  fs(c++,"UTILITY_STATE" ,2,1,3, 1,148,2,1,1 ,000);
  fs(c++,"INRUSH_LIMIT_ON" ,2,1,4, 1,148,2,1,1 ,000);
  fs(c++,"BATTERY_TEST_STATE" ,2,1,6, 1,148,2,1,1 ,000);
  fs(c++,"INPUT_OVERVOLTAGE" ,2,1,7, 1,148,2,1,1 ,000);
  fs(c++,"ON_BATTERY" ,2,1,8, 1,148,2,1,1 ,000);
  fs(c++,"ON_BYPASS" ,2,1,0, 1,148,2,1,2 ,000);
  fs(c++,"BATTERY_CHARGED" ,2,1,1, 1,148,2,1,2 ,000);
  fs(c++,"BATTERY_LIFE_ENHANCER_ON" ,2,1,4, 1,148,2,1,2 ,000);
  fs(c++,"REPLACE_BATTERY" ,2,1,5, 1,148,2,1,2 ,000);
  fs(c++,"BOOST_ON" ,2,1,6, 1,148,2,1,2 ,000);
  fs(c++,"DIAG_LINK_SET" ,2,1,7, 1,148,2,1,2 ,000);
  fs(c++,"BUCK_ON" ,2,1,9, 1,148,2,1,2 ,000);
  fs(c++,"UPS_OVERLOAD" ,2,1,0, 1,148,2,1,3 ,000);
  fs(c++,"BAD_INPUT_FREQ" ,2,1,1, 1,148,2,1,3 ,000);
  fs(c++,"SHUTDOWN_PENDING" ,2,1,2, 1,148,2,1,3 ,000);
  fs(c++,"CHARGER_FAIL" ,2,1,3, 1,148,2,1,3 ,000);
  fs(c++,"LOW_BATTERY" ,2,1,5, 1,148,2,1,3 ,000);
  fs(c++,"OUTPUT_UNDERVOLTAGE" ,2,1,6, 1,148,2,1,3 ,000);
  fs(c++,"OUTPUT_OVERVOLTAGE" ,2,1,7, 1,148,2,1,3 ,000);
  fs(c++,"BAD_BYPASS_PWR" ,2,1,8, 1,148,2,1,3 ,000);
  fs(c++,"CHECK_AIR_FILTER" ,2,1,10, 1,148,2,1,3 ,000);
  fs(c++,"AMBIENT_OVERTEMP" ,2,1,2, 1,148,2,1,7 ,000);
  fs(c++,"BATTERY_TEST_RESULT", 0,0,1, 1,148,2,1,12 ,000);
  fs(c++,"SELF_TEST_RESULT", 0,0,1, 1,148,2,1,13 ,000);
  fs(c++,"FAILED_POWER_MODULES", 2,8,0, 1,148,2,1,18 ,000);
  fs(c++,"FAILED_BATTERY_MODULES", 2,8,8, 1,148,2,1,18 ,000);
  fs(c++,"MAIN_CONTROL_MODULE_FAILED" ,2,1,0, 1,148,2,1,19 ,000);
  fs(c++,"REDUNDANT_CONTROL_MODULE_FAILED" ,2,1,1, 1,148,2,1,19 ,000);
  fs(c++,"UI_MODULE_FAILED" ,2,1,2, 1,148,2,1,19 ,000);
  fs(c++,"REDUNDANT_POWER_MODULE_ALARM" ,2,1,3, 1,148,2,1,19 ,000);
  fs(c++,"REDUNDANT_BATTERY_MODULE_ALARM" ,2,1,4, 1,148,2,1,19 ,000);
  fs(c++,"USER_MAX_LOAD_ALARM" ,2,1,5, 1,148,2,1,19 ,000);
  fs(c++,"TRANSFORMER_OVERTEMP_ALARM" ,2,1,6, 1,148,2,1,19 ,000);
  fs(c++,"INTERNAL_COMMS_LOST" ,2,1,7, 1,148,2,1,19 ,000);
  fs(c++,"NEW_EVENT_HISTORY" ,2,1,8, 1,148,2,1,19 ,000);
  fs(c++,"PWR_MOD_FAILED" ,2,1,9, 1,148,2,1,19 ,000);
  fs(c++,"BAT_MOD_FAILED" ,2,1,10, 1,148,2,1,19 ,000);
  fs(c++,"OPTION_CARD_FAIL_1" ,2,1,0, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_2" ,2,1,1, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_3" ,2,1,2, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_4" ,2,1,3, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_5" ,2,1,4, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_6" ,2,1,5, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_7" ,2,1,6, 1,148,2,1,20 ,000);
  fs(c++,"OPTION_CARD_FAIL_8" ,2,1,7, 1,148,2,1,20 ,000);
  fs(c++,"SUMMARY_ALARM" ,2,1,0, 1,148,2,1,22 ,000);
  fs(c++,"RECT_UV_STARTUP_FAIL" ,2,1,0, 1,148,2,1,24 ,000);
  fs(c++,"RECT_FAULT" ,2,1,1, 1,148,2,1,24 ,000);
  fs(c++,"RECT_OVER_CURRENT" ,2,1,2, 1,148,2,1,24 ,000);
  fs(c++,"RECT_OVER_TEMP" ,2,1,3, 1,148,2,1,24 ,000);
  fs(c++,"RECT_INDCTR_OVER_TEMP" ,2,1,4, 1,148,2,1,24 ,000);
  fs(c++,"RECT_COMM_FAIL" ,2,1,5, 1,148,2,1,24 ,000);
  fs(c++,"INV_SHUTDOWN_LOW_DC" ,2,1,6, 1,148,2,1,24 ,000);
  fs(c++,"INV_FAULT" ,2,1,7, 1,148,2,1,24 ,000);
  fs(c++,"INV_OVER_CURRENT" ,2,1,8, 1,148,2,1,24 ,000);
  fs(c++,"INV_OVER_TEMP" ,2,1,9, 1,148,2,1,24 ,000);
  fs(c++,"INV_INDCTR_OVER_TEMP" ,2,1,10, 1,148,2,1,24 ,000);
  fs(c++,"INV_COMM_FAIL" ,2,1,11, 1,148,2,1,24 ,000);
  fs(c++,"INV_DC_OFFSET_OVR" ,2,1,12, 1,148,2,1,24 ,000);
  fs(c++,"INV_CONTACTOR_FAIL" ,2,1,13, 1,148,2,1,24 ,000);
  fs(c++,"BAT_CHARGE_STATE", 0,0,1, 1,148,2,1,25 ,000);
  fs(c++,"BAT_FAULT" ,2,1,1, 1,148,2,1,25 ,000);
  fs(c++,"BAT_CONTACTOR_FAIL" ,2,1,2, 1,148,2,1,25 ,000);
  fs(c++,"CONVERTER_OVER_TEMP" ,2,1,4, 1,148,2,1,25 ,000);
  fs(c++,"CONVERTER_OVER_AMPS" ,2,1,5, 1,148,2,1,25 ,000);
  fs(c++,"CONVERTER_FAIL" ,2,1,6, 1,148,2,1,25 ,000);
  fs(c++,"BALANCER_OVER_TEMP" ,2,1,7, 1,148,2,1,25 ,000);
  fs(c++,"BALANCER_FAULT" ,2,1,8, 1,148,2,1,25 ,000);
  fs(c++,"BALANCER_OVER_CURRENT" ,2,1,9, 1,148,2,1,25 ,000);
  fs(c++,"BY_CB_OPEN" ,2,1,10, 1,148,2,1,25 ,000);
  fs(c++,"LOAD_IMPACT_XFER" ,2,1,11, 1,148,2,1,25 ,000);
  fs(c++,"OPERATION_FAULT" ,2,1,12, 1,148,2,1,25 ,000);
  fs(c++,"OUT_FUSE_BLOWN" ,2,1,13, 1,148,2,1,25 ,000);
  fs(c++,"ON_JOINT_MODE" ,2,1,14, 1,148,2,1,25 ,000);
  fs(c++,"GENERATOR_DISC", 0,0,1, 1,148,2,1,25 ,000);
  fs(c++,"ROTARY_BREAKER", 0,0,1, 1,148,2,1,26 ,000);
  fs(c++,"MAIN_NEUTRAL_LOST" ,2,1,4, 1,148,2,1,26 ,000);
  fs(c++,"LOAD_SOURCE", 0,0,1, 1,148,2,1,27 ,000);

  fs(c++,"PARALLEL_LOW_BAT_WARN" ,2,1,4, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_LOAD_SHARE_FAULT" ,2,1,5, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_FAULT" ,2,1,6, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_CONNECT_FAULT" ,2,1,7, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_COMM_FAIL" ,2,1,8, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_SYS_OVER_LOAD" ,2,1,9, 1,148,2,1,27 ,000);
  fs(c++,"PARALLEL_SYS_XFER" ,2,1,10, 1,148,2,1,27 ,000);

  fs(c++,"SYSVIEW_OUT_WATT_PH_A", 0,0,0.01, 1,154,2,1,1 ,000);
  fs(c++,"SYSVIEW_OUT_VA_PH_A", 0,0,0.01, 1,154,2,1,2 ,000);
  fs(c++,"SYSVIEW_OUT_VAR_PH_A", 0,0,0.01, 1,154,2,1,3 ,000);
  fs(c++,"SYSVIEW_OUT_WATT_PH_B", 0,0,0.01, 1,154,2,1,4 ,000);
  fs(c++,"SYSVIEW_OUT_VA_PH_B", 0,0,0.01, 1,154,2,1,5 ,000);
  fs(c++,"SYSVIEW_OUT_VAR_PH_B", 0,0,0.01, 1,154,2,1,6 ,000);
  fs(c++,"SYSVIEW_OUT_WATT_PH_C", 0,0,0.01, 1,154,2,1,7 ,000);
  fs(c++,"SYSVIEW_OUT_VA_PH_C", 0,0,0.01, 1,154,2,1,8 ,000);
  fs(c++,"SYSVIEW_OUT_VAR_PH_C", 0,0,0.01, 1,154,2,1,9 ,000);

  c->init=0; /*on first non-init element */

}

int GetCmdCount(cmd_s * c)
{
  int i;
  for (i=0;(i<NCMD)&&c++->init;i++) ;
  return i;

}

/*return array position of command "descr"*/
int GetCmdIdbyDesc(cmd_s * c,char *descr)
{
  int i;
  for (i=0;i<NCMD;i++) {
    if (!strcmp(descr,c->descr))  return i;
    c++;
  }
  return -1;
}

/*return array position of the same status UPS command with the most recent data
 *this is since each status command includes several statuses, and we don't want
 *to re-query the UPS for each of those statuses if the status is recent enough*/
int GetRecentStatusId(cmd_s *c,unsigned char bit4)
{
  int i,r=-1;
  time_t maxt=0;
  

  for (i=0;(i<NCMD)&&c++->init;i++) {
    if ((c->cmd[1]!=148) || (c->cmd[4]!=bit4))  
      continue;
    else if (c->when>maxt)  {
      maxt=c->when;
      r=i;
    }
  }

  return r; 
  
}
/***************************************************/


void sgnl_ignore(int status)
 {
   fprintf(stderr,"Signaled\n");
 }

/*calculate checksum*/
unsigned char cksum(unsigned char *buf, int len)
 {
   unsigned char sum=0;
   while(len-->0)
      sum+=*buf++;
   return(sum);
 }


int timedwrite(int fd, unsigned char *buf, int len, int msec)
{
  fd_set wfds;
  struct timeval tv;
  int retval;

  int writ=0;
  while(writ<len) {
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = msec/1000;
    tv.tv_usec = (msec%1000)*1000;

    retval = select(fd+1, NULL, &wfds, NULL, &tv);
    if( retval>=0 && FD_ISSET(fd, &wfds) )
     {
       int res = write(fd,buf+writ,len-writ);
       if( res>0 )
	  writ+=res;
       else
	  return(-1);
     }
    if( retval<=0 )
       return(-1);
  }
  return(writ);
}


int timedread(int fd, unsigned char *buf, int len, int msec)
{
  fd_set rfds;
  struct timeval tv;
  int retval;

  int red=0;
  while(red<len) {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    tv.tv_sec = msec/1000;
    tv.tv_usec = (msec%1000)*1000;
    retval = select(fd+1, &rfds, NULL, NULL, &tv);

    if( retval>=0 && FD_ISSET(fd, &rfds) )
     {
       int res = read(fd,buf+red,len-red);
       if( res>0 )
	  red+=res;
       else
	  return(-1);
     }
    if( retval<=0 )
       return(-1);
  }
  return(red);
}


/* Send a command and read a measurement */
int SendCmd_M(int fd, unsigned char *cmd, unsigned char *rbyteH, unsigned char *rbyteL, char * descr,int rfmt)
{
   unsigned char buf[8];
   int res;

   cmd[5]=cksum(cmd,5);


   if (debug)
    fprintf(stderr,"\nSendingM: %03d,%03d,%03d,%03d,%03d,%03d\t%s,bit/divider:%d\n",cmd[0],cmd[1],cmd[2],cmd[3],cmd[4],cmd[5],descr,rfmt);

   res=timedwrite(fd,cmd,6,500);
   if(res!=6){
     fprintf(stderr,"port write error, cmd:");
     fprintf(stderr,"%03d,%03d,%03d,%03d,%03d,%03d\n",cmd[0],cmd[1],cmd[2],cmd[3],cmd[4],cmd[5]);
     return 65000; /*values will never be that high, values with MSB >128 are negative*/
   }

   res=timedread(fd,buf,8,200);
   if (debug)
    fprintf(stderr,"READ    : %03d,%03d,%03d,%03d,%03d, %03d,%03d (%03d %03d), %03d\n",
    buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6], buf[5],buf[6], buf[7]);

   if(res!=8){
     fprintf(stderr,"read count error (%d<>8),cmd:",res);
     fprintf(stderr,"%03d,%03d,%03d,%03d,%03d,%03d\n",cmd[0],cmd[1],cmd[2],cmd[3],cmd[4],cmd[5]);
     return 65000;
   }

   if(buf[7]!=cksum(buf,7)){
     fprintf(stderr,"checksum error for cmd:");
     fprintf(stderr,"%03d,%03d,%03d,%03d,%03d,%03d\n",cmd[0],cmd[1],cmd[2],cmd[3],cmd[4],cmd[5]);
     return 65000;
   }


   res= (signed short int)(((short)buf[5])*256+buf[6]);

   *rbyteH=(unsigned short)buf[5];
   *rbyteL=(unsigned short)buf[6];
   return res;
}

/* Send a series of consecutive commands and read text response (2 chars each)*/
int SendCmd_T(int fd, unsigned char *cmd, char *chrs, int len, char * descr)
 {
   unsigned char buf[8], lcmd[7];
   int wres,rres;
   buf[0]=0;

   *chrs=0;
   memcpy(lcmd,cmd,6);
   for(;len>0;len--)
    {
      lcmd[5]=cksum(lcmd,5);
      if (debug)
	fprintf(stderr,"\nSendingT: %03d,%03d,%03d,%03d,%03d,%03d\t%s,length:%d\n",lcmd[0],lcmd[1],lcmd[2],lcmd[3],lcmd[4],lcmd[5],descr,len);
      wres=timedwrite(fd,lcmd,6,200);
      if (wres!=6)
	return -1;
      rres=timedread(fd,buf,8,200);
      if (debug)
	fprintf(stderr,"READ    : %03d,%03d,%03d,%03d,%03d,%03d,%03d,%03d\n",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
      if (rres!=8)
	return -1;

      *chrs++=buf[6];
      *chrs++=buf[5];
    
      if (buf[7]!=cksum(buf,7)) 
	return -1;
      lcmd[4]++;
    }
   *chrs=0;
   
   return 1;
 }

void portsetup(char * port)
{

  fd = open(port, O_RDWR | O_NOCTTY );
  if (fd < 0) {
    fprintf(stderr,"error %d\n",errno);
    sprintf(str,"%s:%s",port,strerror(errno));
    fprintf(stderr,"%s\n",str);
    exit(errno); 
  }

  tcgetattr(fd,&oldtio); /* save current port settings */
  bzero(&newtio, sizeof(newtio));
  newtio.c_cflag = BAUDRATE /*| CRTSCTS*/ | CS8 | CLOCAL | CREAD;
   newtio.c_iflag = IGNPAR | IGNBRK;
  newtio.c_oflag = 0;
   /* set input mode (non-canonical, no echo,...) */
  newtio.c_lflag = 0;
  newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
  newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */
  tcflush(fd, TCIFLUSH);
  tcsetattr(fd,TCSANOW,&newtio);


}

/*Read data from UPS corresponding to c->cmd and fill-in result in struct cmd_s*/
int ReadCmd(cmd_s * c) {
  int r,res;
  int j;
  unsigned char rbyteH,rbyteL;
  char buf[255];

  if (c->supported==0) {      /*read failed on 1st try, thus marked as not supported*/
     /*printf("UNSUPPORTED item %s:skipping\n",c->descr);*/
     return 0;
  }
  if (c->type==0) {      /*measurement*/
    /*todo: check if time is fresh, and if not then SendCmd. Same for strings*/
    res=SendCmd_M(fd, c->cmd, &rbyteH, &rbyteL,c->descr,c->rfmt);
    if(res!=65000) {
      signed short int sh;
      c->when=time(0); 
      c->rbyteL=rbyteL;
      c->rbyteH=rbyteH; 
      sh=(signed short int)(256*c->rbyteH+c->rbyteL);
      printf("%s: %.1f\n",c->descr,(signed short int)(256*c->rbyteH+c->rbyteL)/(float)c->rfmt); 
    }
     else  {
       printf("Error getting %s:marking as not supported\n",c->descr);
       c->supported=0;
     }
  }
  else if (c->type==1) { /*ascii*/
    res=SendCmd_T(fd, c->cmd, buf, c->length,c->descr);  
    if (res!=65000) {
      strcpy(c->rstr,buf);
      c->when=time(0);
      printf("%s: %s\n",c->descr,c->rstr); 
    }
    else {
      printf("Error getting %s:marking as not supported\n",c->descr);
      c->supported=0;
    }
  }
  else if ((c->type==2)&& (c->length<2)) { /*status*/
    r=GetRecentStatusId(&c[0],c->cmd[4]);
    if ((r<0) || (c->when-time(0)>MAXAGE)) { /*not previous data or old data*/
       res=SendCmd_M(fd, c->cmd, &rbyteH, &rbyteL,c->descr,c->rfmt);
       if (res!=65000) {
	 c->when=time(0);
	 c->rbyteL=rbyteL;
	 c->rbyteH=rbyteH;

	 if (c->rfmt<8)  (j=IsBitSet(c->rbyteL,(unsigned short int)c->rfmt));
	 else (j=IsBitSet(c->rbyteH,(unsigned short int)c->rfmt-8) );

	 if (j) printf("%s: YES\n",c->descr);
	 else printf("%s: NO\n",c->descr);
       }
       else {
	 printf("Error getting %s:marking as not supported\n",c->descr);
	 c->supported=0;
       }
    }
    else { /*found recent data from other poll*/
       if (c->rfmt<8)  (j=IsBitSet(c[r].rbyteL,(unsigned short int)c->rfmt));
       else (j=IsBitSet(c[r].rbyteH,(unsigned short int)c[r].rfmt-8) );

       if (j) printf("%s: YES(cached)\n",c->descr);
       else printf("%s: NO(cached)\n",c->descr);
    }
  }

  else if ((c->type==2)&& (c->length>=2)) { /*measurement in bits*/
       res=SendCmd_M(fd, c->cmd, &rbyteH, &rbyteL,c->descr,c->rfmt);
       if (res!=65000) {
	 int bitn;
	 unsigned int value;
	 value=0;
	 c->when=time(0);
	 c->rbyteL=rbyteL;
	 c->rbyteH=rbyteH;

	 if (c->rfmt<8){
	   for (bitn=c->rfmt;bitn<(c->rfmt+c->length);bitn++) {
	     if (IsBitSet(rbyteL,(unsigned short int)bitn))
	       value+=(1<<(unsigned short int)(bitn-c->rfmt));
	   }
	 }
	 else {
	   int rf=c->rfmt-8;
	   for (bitn=rf;bitn<(rf+c->length);bitn++) {
	     if (IsBitSet(rbyteH,(unsigned short int)bitn))
	       value+=1<<(bitn-rf);
	   }
	 }
	 printf("%s: %d\n",c->descr,(signed short int)(value)); 
       }
       else {
	 printf("Error getting %s:marking as not supported\n",c->descr);
	 c->supported=0;
       }
  }
  return c->supported;
}

void usage () {
  fprintf(stderr,"upsesp2 version %s:\n",version);
  fprintf(stderr,"Usage:\n");
  fprintf(stderr,"\t\tupsesp2 [-d] [-l] [-p parameter] <serial device>\n");
  fprintf(stderr,"\t\t-d\tprint debug information\n");
  fprintf(stderr,"\t\t-l\tloop indefinitely\n");
  fprintf(stderr,"\t\t-p\tquery only specified UPS parameter.\n");
  fprintf(stderr,"\n\t\texample:  upsesp2 -c OUTPUT_VOLTAGE /dev/ttyS1\n");
}

int main(int argc, char **argv)
 {
  int i;
  unsigned int ncmd;
  cmd_s c[NCMD];
  int reqcmd=-1; /*if requesting specific parameter*/
  int opt;
  int doloop=0;


  setvbuf(stdout, NULL, _IONBF, 0); 

  initcmd(&c[0]); /* fill in struct table */
  ncmd=GetCmdCount(&c[0]); /*count number of commands*/

  while ((opt = getopt(argc, argv, "ldp:")) != -1) {
    switch (opt) {
    case 'd':
	debug = 1;
	break;
    case 'l':
	doloop = 1;
	break;
    case 'p':
	reqcmd=GetCmdIdbyDesc(&c[0],optarg); /*get position of requested parameter in c[]*/
	break;
    default: /* '?' */
        usage();
	exit(EXIT_FAILURE);
     }
  }


  if (optind >= argc) {
     usage();
     fprintf(stderr, "\nerror: expected serial device (e.g. /dev/ttyS0) after options\n");
     exit(EXIT_FAILURE);
  }

  printf("device specified = %s\n", argv[optind]);

  portsetup(argv[optind]); /*setup serial port*/
  signal( SIGALRM, &sgnl_ignore ); /*for select*/

  if (reqcmd>=0) { /* if requesting a single ups command (measurement/status)*/
    ReadCmd(&c[reqcmd]);
  }
  else
    while (1) {
      for (i=0;i<ncmd;i++) { /*loop through all defined ups commands */
	ReadCmd(&c[i]);
        if (!c[i].supported) 
	  printf("%s not supported on this UPS\n",c[i].descr);
      }
      if (!doloop) {
	break;
      }
      printf("Sleeping....\n\n");
      sleep(2);
    }

  tcsetattr(fd,TCSANOW,&oldtio);

  return 0;

} /* main() */
