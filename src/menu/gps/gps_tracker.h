/**
 * @file gps_tracker.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief GPS tracker
 * @version 0.1
 * @date 2024-11-20
 */

#ifndef __GPS_TRACKER_H__
#define __GPS_TRACKER_H__

#include <TinyGPS++.h>
#include <globals.h>

class GPSTracker {
public:
    GPSTracker();
    ~GPSTracker();

    void setup();
    void loop();

private:
    bool date_time_updated = false;
    bool initial_position_set = false;
    double cur_lat = 0;
    double cur_lng = 0;
    double distance = 0;
    String filename = "";
    TinyGPSPlus gps;
    HardwareSerial GPSserial = HardwareSerial(2);
    int gpsCoordCount = 0;
    bool rxPinReleased = false;

    // Fallback parse for talkers TinyGPS misses (e.g. $BDGGA) + UI diagnostics
    String nmeaLine;
    double fbLat = 0;
    double fbLng = 0;
    int fbQuality = 0;
    int fbSats = 0;
    char fbRmcStatus = '-';
    uint32_t fbFixMs = 0;

    bool begin_gps(void);
    void end(void);
    void releasePins(void);
    void restorePins(void);

    void noteNmeaChar(char c);
    bool fallbackFixFresh() const;
    bool saneGpsDateTime();

    void set_position(void);
    void add_coord(void);
    void add_initial_file_data(File file);
    void add_final_file_data(void);
    void create_filename(void);
};

#endif // GPS_TRACKER_H
