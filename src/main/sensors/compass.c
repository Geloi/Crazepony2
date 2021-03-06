/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>

#include <platform.h>

#include "build/build_config.h"

#include "common/axis.h"

#include "config/parameter_group.h"
#include "config/parameter_group_ids.h"
#include "config/profile.h"

#include "drivers/sensor.h"
#include "drivers/compass.h"
#include "drivers/compass_hmc5883l.h"
#include "drivers/gpio.h"
#include "drivers/light_led.h"

#include "sensors/boardalignment.h"

#include "fc/runtime_config.h"
#include "fc/config.h"

#include "sensors/sensors.h"
#include "sensors/compass.h"

#ifdef NAZE
#include "hardware_revision.h"
#endif

PG_REGISTER_PROFILE_WITH_RESET_TEMPLATE(compassConfig_t, compassConfig, PG_COMPASS_CONFIGURATION, 0);

PG_RESET_TEMPLATE(compassConfig_t, compassConfig,
    .mag_declination = 0,
);

mag_t mag;                   // mag access functions

float magneticDeclination = 0.0f;

extern uint32_t currentTime; // FIXME dependency on global variable, pass it in instead.

int16_t magADCRaw[XYZ_AXIS_COUNT];
int32_t magADC[XYZ_AXIS_COUNT];
sensor_align_e magAlign = 0;
#ifdef MAG
static uint8_t magInit = 0;

void compassInit(void)
{
    // initialize and calibration. turn on led during mag calibration (calibration routine blinks it)
    LED1_ON;
    mag.init();
    LED1_OFF;
    magInit = 1;
}

void updateCompass(flightDynamicsTrims_t *magZero)
{
    static uint32_t tCal = 0;
    static flightDynamicsTrims_t magZeroTempMin;
    static flightDynamicsTrims_t magZeroTempMax;
    uint32_t axis;

    mag.read(magADCRaw);
    for (axis = 0; axis < XYZ_AXIS_COUNT; axis++) magADC[axis] = magADCRaw[axis];  // int32_t copy to work with
    alignSensors(magADC, magADC, magAlign);

    if (STATE(CALIBRATE_MAG)) {
        tCal = currentTime;
        for (axis = 0; axis < 3; axis++) {
            magZero->raw[axis] = 0;
            magZeroTempMin.raw[axis] = magADC[axis];
            magZeroTempMax.raw[axis] = magADC[axis];
        }
        DISABLE_STATE(CALIBRATE_MAG);
    }

    if (magInit) {              // we apply offset only once mag calibration is done
        magADC[X] -= magZero->raw[X];
        magADC[Y] -= magZero->raw[Y];
        magADC[Z] -= magZero->raw[Z];
    }

    if (tCal != 0) {
        if ((currentTime - tCal) < 30000000) {    // 30s: you have 30s to turn the multi in all directions
            LED0_TOGGLE;
            for (axis = 0; axis < 3; axis++) {
                if (magADC[axis] < magZeroTempMin.raw[axis])
                    magZeroTempMin.raw[axis] = magADC[axis];
                if (magADC[axis] > magZeroTempMax.raw[axis])
                    magZeroTempMax.raw[axis] = magADC[axis];
            }
        } else {
            tCal = 0;
            for (axis = 0; axis < 3; axis++) {
                magZero->raw[axis] = (magZeroTempMin.raw[axis] + magZeroTempMax.raw[axis]) / 2; // Calculate offsets
            }

            saveConfigAndNotify();
        }
    }
}
#endif

void recalculateMagneticDeclination(void)
{
    int16_t deg, min;

    if (sensors(SENSOR_MAG)) {
        // calculate magnetic declination
        deg = compassConfig()->mag_declination / 100;
        min = compassConfig()->mag_declination % 100;

        magneticDeclination = (deg + ((float)min * (1.0f / 60.0f))) * 10; // heading is in 0.1deg units
    } else {
        magneticDeclination = 0.0f; // TODO investigate if this is actually needed if there is no mag sensor or if the value stored in the config should be used.
    }

}
