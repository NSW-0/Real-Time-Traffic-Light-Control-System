#include "traffic.h"

const char *DIR_NAME[DIR_COUNT]  = {"North","South","East","West"};
const char *LIGHT_NAME[3]        = {"RED","YELLOW","GREEN"};
const char *PHASE_NAME[PHASE_COUNT] = {
    "NS-GREEN","NS-YELLOW","ALL-RED-1",
    "EW-GREEN","EW-YELLOW","ALL-RED-2",
    "PEDESTRIAN","EMERGENCY"
};
