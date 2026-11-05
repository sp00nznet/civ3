#include <math.h>
#include <stdio.h>
#include "sfx_softlimit.h"
static int errors;
static void check(const char *message,int ok) {
    if(!ok){fprintf(stderr,"FAIL: %s\n",message);errors++;}
}
int main(void) {
    check("zero preserved",civ3_sfx_soft_limit(0.0f)==0.0f);
    check("quiet PCM untouched",civ3_sfx_soft_limit(0.12f)==0.12f &&
          civ3_sfx_soft_limit(-0.12f)==-0.12f);
    check("near threshold untouched",civ3_sfx_soft_limit(0.80f)==0.80f);
    check("loud positive is reduced without muting",civ3_sfx_soft_limit(1.8f)>0.8f &&
          civ3_sfx_soft_limit(1.8f)<1.0f);
    check("negative symmetry",fabsf(civ3_sfx_soft_limit(-2.0f) +
                                    civ3_sfx_soft_limit(2.0f))<0.000001f);
    float previous=-1.0f;
    for(int i=-20000;i<=20000;i++) {
        float x=i/1000.0f;
        float output=civ3_sfx_soft_limit(x);
        check("monotonic soft limiting",output >= previous - 1e-6f);
        check("output always in 16-bit range",output>=-1.0f && output<=1.0f);
        if(fabsf(x)<=0.8f) check("linear quiet dynamics preserved",fabsf(x-output)<1e-6f);
        previous=output;
    }
    if(!errors) puts("PASS: soft limiter preserves quiet music/effects, softens excess SFX peaks and bounds full-scale output");
    return errors?1:0;
}
