# Media And Sensor API Patterns

These interfaces are integration patterns, not declarations guaranteed to exist in the repository.
Reuse an existing service when present; otherwise adapt the smallest applicable pattern under
src/platform.

## Camera

Suggested API shape:

~~~cpp
namespace platform::camera {
struct CameraInfo {
  std::string name;
  int width{320};
  int height{240};
};

bool find_mipi_csi_camera(CameraInfo& camera, std::string& error);

class PreviewSession {
 public:
  bool start(const CameraInfo& camera, std::string& error);
  bool copy_frame_rgb565(std::vector<uint16_t>& frame, int& width, int& height) const;
  bool running() const;
  void stop();
};
}
~~~

Usage: discover one camera, start a session, copy the latest complete RGB565 frame from a UI timer
or worker, and stop the session before destroying its LVGL image buffer. PreviewSession should be
non-copyable and release native requests, mapped buffers, and camera ownership in its destructor.

The device backend uses libcamera and normally links camera plus camera-base. Camera discovery,
start, and frame conversion may block; keep them outside LVGL callbacks. A desktop fallback must be
labeled simulation or return unsupported. Set upload permission camera.

## Audio And Headphone State

Suggested one-shot API:

~~~cpp
namespace platform::audio {
struct AudioDevice {
  std::string backend_name;
  std::string display_name;
  std::string playback_device;
  std::string capture_device;
};

bool find_audio_device(AudioDevice& device, std::string& error);
bool record_wav(const AudioDevice& device, const std::string& path, int seconds);
bool play_wav(const AudioDevice& device, const std::string& path);
void set_volume_level(float level);
}
~~~

Run recording and playback on a worker. Stop UI sounds before exclusive recording, use an
application-owned writable path, restore the previous volume/session state, and report failures
instead of treating silence as success. The backend uses miniaudio; capture requires upload
permission microphone.

For reusable UI sounds, keep an enum-to-asset mapping and expose initialize, play, stop, and
shutdown operations. Initialize only after asset paths and volume are configured; always shut the
audio engine down during application teardown.

Headphone state can use:

~~~cpp
bool read_headphone_inserted(bool& inserted, std::string& error);
void release_headphone_device();
~~~

Release any retained input/GPIO handle during teardown. Confirm the package schema before assigning
a dedicated permission.

## IMU

Suggested API shape:

~~~cpp
namespace platform::imu {
struct ImuDevice {
  std::string i2c_path;
  std::string iio_path;
  std::string mag_iio_path;
  bool has_bmi270{false};
  bool has_bmm150{false};
};

struct NineAxisReading {
  double accel_x, accel_y, accel_z;
  double gyro_x, gyro_y, gyro_z;
  double magn_x, magn_y, magn_z;
};

bool find_bmi270_device(ImuDevice& device, std::string& error);
bool read_nine_axis(const ImuDevice& device, NineAxisReading& reading, std::string& error);
}
~~~

Discovery identifies BMI270/BMM150 through target Linux I2C and IIO sysfs entries. Reads combine raw
accelerometer, gyroscope, and magnetometer values with the matching scale attributes. Preserve
partial capability information: six-axis data can remain usable when the magnetometer is absent.

Discover once, poll at a bounded interval, stop polling before screen teardown, and surface read
errors without re-running full discovery on every frame. Desktop builds should return unsupported
or clearly simulated values. Set upload permission imu.
