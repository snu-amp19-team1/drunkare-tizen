#include <string>
#include <iostream>
#include <queue>
#include <deque>
#include <thread>
#include <memory>
#include <curl/curl.h>
#include <sstream>

// Tizen libraries
#include <sensor.h>
#include <efl_util.h>
#include <device/power.h>
#include <Elementary.h>
#include <pthread.h>

#include "drunkare.h"
#include "queue.h"
#include "data.h"

#define NUM_SENSORS 2
#define NUM_CHANNELS 3
#define DURATION 12 // seconds
#define ACCELEROMETER 0
#define GYROSCOPE 1

using TMeasure = Measure<NUM_CHANNELS, DURATION>;

struct appdata_s {
  Evas_Object *win;
  Evas_Object *conform;
  Evas_Object *label;
  Evas_Object *button;

  // Extra app data
  bool _isMeasuring;
  sensor_h sensors[NUM_SENSORS];
  sensor_listener_h listners[NUM_SENSORS];
  int _deviceSamplingRate = 10;
  std::vector<int> _measureId;
  std::deque<std::unique_ptr<TMeasure>> tMeasures[NUM_SENSORS];
  pthread_t netWorker; /* the thread identifier for netWorkerJob */
  Queue<TMeasure> queue;
  std::string hostname;
  long port;
  uint64_t nrSentSamples;
  std::string username;
  std::string url;

  appdata_s() : win(nullptr) {}
};

static void win_delete_request_cb(void *data, Evas_Object *obj,
                                  void *event_info) {
  ui_app_exit();
}

static void
win_back_cb(void *data, Evas_Object *obj, void *event_info)
{
	appdata_s *ad = (appdata_s *)data;
        /* Let window go to hide state. */
	elm_win_lower(ad->win);
}

//
// Main function for `netWorker`.
//   1. Dequeue a `Measure` from `ad->queue`
//   2. Format POST fields
//   3. Send POST request to <hostname:port>
//   4. Repeat
//
static void *netWorkerJob(void* data) {
  appdata_s* ad = (appdata_s*) data;
  CURL *curl;
  CURLcode res;

  while (true) {
    // Get Measure pointer
    dlog_print(DLOG_ERROR, "RACE_HUNTER", "[+] Enter loop");
    auto tMeasure = ad->queue.dequeue();
    if (!tMeasure) {
      dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] tMeasure == nullptr");
      break;
    }

    dlog_print(DLOG_ERROR, "RACE_HUNTER", "");

    /* JSON formatting */
    std::string jsonObj = tMeasure->format();
    std::string url = ad->hostname + ":" + std::to_string(ad->port) + "/" + ad->username;
    elm_object_text_set(ad->label, url.c_str());

    /* Curl POST */
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "charsets: utf-8");

    dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] curl_slist_append");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonObj.c_str());
    // curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] res != CURLE_OK?");
    	pthread_exit(NULL);
    }

    curl_easy_cleanup(curl);
    dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] curl_easy_cleanup");
    curl_global_cleanup();
    dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] curl_global_cleanup");
    // usleep(3000);
  }

  return NULL;
}

void sensorCb(sensor_h sensor, sensor_event_s *event, void *user_data)
{
  appdata_s *ad = (appdata_s *)user_data;
  int sensor_type;
  sensor_type_e type;
  sensor_get_type(sensor, &type);
  std::vector<float> values;
  unsigned long long timestamp;

  switch (type) {
  case SENSOR_ACCELEROMETER:
    sensor_type = ACCELEROMETER;
    break;
  case SENSOR_GYROSCOPE:
    sensor_type = GYROSCOPE;
    break;
  default:
    return;
  }

  // TODO: refer to https://github.com/snu-amp19-team1/queue
  // Save temporary value array
  for (int i = 0; i < NUM_CHANNELS; i++) {
    values.push_back(event->values[i]);
  }

  // Check tMeasures deque
  if (ad->tMeasures[sensor_type].empty()) {
	  ad->tMeasures[sensor_type].push_back(std::make_unique<TMeasure>(ad->_measureId[sensor_type]++, sensor_type));
	  timestamp = (unsigned long long)time(NULL);
	  ad->tMeasures[sensor_type].front()->setTimestamp(timestamp);
  }

  // Tick (store values in Measure.data every periods)
  ad->tMeasures[sensor_type].front()->tick(values);

  // Check Measure->_done and enqueue
  if (ad->tMeasures[sensor_type].front()->_done) {
    ad->queue.enqueue(std::move(ad->tMeasures[sensor_type].front()));
    ad->tMeasures[sensor_type].pop_front();
  }
}

static void startMeasurement(appdata_s *ad)
{
  for (int i = 0; i < NUM_SENSORS; i++) {
    sensor_listener_set_event_cb(ad->listners[i], ad->_deviceSamplingRate,
                                 sensorCb, ad);
    sensor_listener_start(ad->listners[i]);
  }
  ad->_isMeasuring = true;
}
static void stopMeasurement(appdata_s *ad)
{
  // TODO
  for (int i = 0; i < NUM_SENSORS; i++) {
    sensor_listener_stop(ad->listners[i]);
  }
  ad->_isMeasuring = false;
  ad->queue.forceDone();
}

static void btnClickedCb(void *data, Evas_Object *obj, void *event_info)
{
  // 1. Set screen always on (This is due to hardware limitation)
  efl_util_set_window_screen_mode(((appdata_s *)data)->win,
                                  EFL_UTIL_SCREEN_MODE_ALWAYS_ON);

  // 2. 
  if (!(((appdata_s *) data)->_isMeasuring)) {
    startMeasurement((appdata_s *) data /* more arguments? */);
    elm_object_text_set(((appdata_s *)data)->button, "Stop");
  } else {
    stopMeasurement((appdata_s *) data /* more arguments? */);
    elm_object_text_set(((appdata_s *)data)->button, "Start");
  }
}

static void
init_button(appdata_s *ad,
            void (*cb)(void *data, Evas_Object *obj, void *event_info))
{
  ad->button = elm_button_add(ad->win);
  evas_object_smart_callback_add(ad->button, "clicked", cb, ad);
  evas_object_move(ad->button, 110, 150);
  evas_object_resize(ad->button, 140, 60);
  elm_object_text_set(ad->button, "Start");
  evas_object_show(ad->button);
}

static void
create_base_gui(appdata_s *ad)
{
	/* Window */
	/* Create and initialize elm_win.
	   elm_win is mandatory to manipulate window. */
	ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
	elm_win_autodel_set(ad->win, EINA_TRUE);

	if (elm_win_wm_rotation_supported_get(ad->win)) {
		int rots[4] = { 0, 90, 180, 270 };
		elm_win_wm_rotation_available_rotations_set(ad->win, (const int *)(&rots), 4);
	}

	evas_object_smart_callback_add(ad->win, "delete,request", win_delete_request_cb, NULL);
	eext_object_event_callback_add(ad->win, EEXT_CALLBACK_BACK, win_back_cb, ad);

	/* Conformant */
	/* Create and initialize elm_conformant.
	   elm_conformant is mandatory for base gui to have proper size
	   when indicator or virtual keypad is visible. */
	ad->conform = elm_conformant_add(ad->win);
	elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
	elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);
	evas_object_size_hint_weight_set(ad->conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_win_resize_object_add(ad->win, ad->conform);
	evas_object_show(ad->conform);

	/* Label */
	/* Create an actual view of the base gui.
	   Modify this part to change the view. */
	ad->label = elm_label_add(ad->conform);
        evas_object_move(ad->label, 0, 100);
	evas_object_size_hint_weight_set(ad->label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_object_content_set(ad->conform, ad->label);

    /* Custom initializations are here! */
    ad->_isMeasuring = false;
    ad->hostname = "localhost";
    ad->username = "anonymous";
    ad->port = 8888;
    ad->nrSentSamples = 0;
    ad->url = ad->hostname + ":" + std::to_string(ad->port) + "/" + ad->username;

    /* Create a thread */
    int error = pthread_create(&(ad->netWorker), NULL, netWorkerJob, ad);
    if (error) {
      dlog_print(DLOG_ERROR, "RACE_HUNTER", "[-] pthread_create");
      return;
    }

    init_button(ad, btnClickedCb);
    sensor_get_default_sensor(SENSOR_ACCELEROMETER, &ad->sensors[ACCELEROMETER]);
    sensor_get_default_sensor(SENSOR_GYROSCOPE, &ad->sensors[GYROSCOPE]);
    for (int i = 0; i < NUM_SENSORS; i++) {
      sensor_create_listener(ad->sensors[i], &ad->listners[i]);
      // Initialize per-sensor measure IDs
      ad->_measureId.push_back(0);
    }

    /* Show window after base gui is set up */
	evas_object_show(ad->win);
}

static bool
app_create(void *data)
{
	/* Hook to take necessary actions before main event loop starts
		Initialize UI resources and application's data
		If this function returns true, the main loop of application starts
		If this function returns false, the application is terminated */
	appdata_s *ad = (appdata_s *)data;

	create_base_gui(ad);

	return true;
}

static void
app_control(app_control_h app_control, void *data)
{
	/* Handle the launch request. */
}

static void
app_pause(void *data)
{
	/* Take necessary actions when application becomes invisible. */
}

static void
app_resume(void *data)
{
	/* Take necessary actions when application becomes visible. */
}

static void
app_terminate(void *data)
{
	/* Release all resources. */
}

static void
ui_app_lang_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LANGUAGE_CHANGED*/
	char *locale = NULL;
	system_settings_get_value_string(SYSTEM_SETTINGS_KEY_LOCALE_LANGUAGE, &locale);
	elm_language_set(locale);
	free(locale);
	return;
}

static void
ui_app_orient_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_DEVICE_ORIENTATION_CHANGED*/
	return;
}

static void
ui_app_region_changed(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_REGION_FORMAT_CHANGED*/
}

static void
ui_app_low_battery(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_BATTERY*/
}

static void
ui_app_low_memory(app_event_info_h event_info, void *user_data)
{
	/*APP_EVENT_LOW_MEMORY*/
}

int
main(int argc, char *argv[])
{
    appdata_s ad;
	int ret = 0;

	ui_app_lifecycle_callback_s event_callback = {0,};
	app_event_handler_h handlers[5] = {NULL, };

    bool supported[NUM_SENSORS];
	sensor_is_supported(SENSOR_ACCELEROMETER, &supported[ACCELEROMETER]);
	sensor_is_supported(SENSOR_GYROSCOPE, &supported[GYROSCOPE]);
	if (!supported[ACCELEROMETER] || !supported[GYROSCOPE]) {
		/* Accelerometer is not supported on the current device */
		return 1;
	}

        // Initialize sensor handles
        // 	sensor_get_default_sensor(SENSOR_ACCELEROMETER, &sensors[ACCELEROMETER]);
        // 	sensor_get_default_sensor(SENSOR_GYROSCOPE, &sensors[GYROSCOPE]);
        // 
        //         // Initialize sensor listeners
        //         for (int i = 0; i < NUM_SENSORS; i++) {
        //           sensor_create_listener(sensors[sensor_index],
        //                                  &listeners[sensor_index]);
        //         }

	event_callback.create = app_create;
	event_callback.terminate = app_terminate;
	event_callback.pause = app_pause;
	event_callback.resume = app_resume;
	event_callback.app_control = app_control;

	ui_app_add_event_handler(&handlers[APP_EVENT_LOW_BATTERY], APP_EVENT_LOW_BATTERY, ui_app_low_battery, &ad);
	ui_app_add_event_handler(&handlers[APP_EVENT_LOW_MEMORY], APP_EVENT_LOW_MEMORY, ui_app_low_memory, &ad);
	ui_app_add_event_handler(&handlers[APP_EVENT_DEVICE_ORIENTATION_CHANGED], APP_EVENT_DEVICE_ORIENTATION_CHANGED, ui_app_orient_changed, &ad);
	ui_app_add_event_handler(&handlers[APP_EVENT_LANGUAGE_CHANGED], APP_EVENT_LANGUAGE_CHANGED, ui_app_lang_changed, &ad);
	ui_app_add_event_handler(&handlers[APP_EVENT_REGION_FORMAT_CHANGED], APP_EVENT_REGION_FORMAT_CHANGED, ui_app_region_changed, &ad);

	ret = ui_app_main(argc, argv, &event_callback, &ad);
	if (ret != APP_ERROR_NONE) {
		dlog_print(DLOG_ERROR, LOG_TAG, "app_main() is failed. err = %d", ret);
	}

	return ret;
}
