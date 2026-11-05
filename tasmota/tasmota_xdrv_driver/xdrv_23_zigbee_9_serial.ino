/*
  xdrv_23_zigbee_9_serial.ino - zigbee: serial communication with MCU

  Copyright (C) 2021  Theo Arends and Stephan Hadinger

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_ZIGBEE

const uint32_t ZIGBEE_LED_SERIAL = 0;     // LED<1> blinks when receiving

#ifdef USE_ZIGBEE_ZNP
const uint32_t ZIGBEE_BUFFER_SIZE = 256;  // Max ZNP frame is SOF+LEN+CMD1+CMD2+250+FCS = 255
const uint8_t  ZIGBEE_SOF = 0xFE;
const uint8_t  ZIGBEE_SOF_ALT = 0xFF;
#endif // USE_ZIGBEE_ZNP

#ifdef USE_ZIGBEE_EZSP
const uint32_t ZIGBEE_BUFFER_SIZE = 256;
const uint8_t  ZIGBEE_EZSP_CANCEL = 0x1A;  // cancel byte
const uint8_t  ZIGBEE_EZSP_EOF = 0x7E;     // end of frame
const uint8_t  ZIGBEE_EZSP_ESCAPE = 0x7D;  // escape byte

class EZSP_Serial_t {
public:
  uint8_t  to_send = 0;     // 0..7, frame number of next packet to send, nothing to send if equal to to_end
  uint8_t  to_end = 0;      // 0..7, frame number of next packet to send
  uint8_t  to_ack = 0;      // 0..7, frame number of last packet acknowledged + 1
  uint8_t  from_ack = 0;    // 0..7, frame to ack
  uint8_t  ezsp_seq = 0;    // 0..255, EZSP sequence number
  SBuffer *to_packets[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
};


EZSP_Serial_t EZSP_Serial;

// Projekte+: ASH receive task (ESP32 only)
//
// The NCP (EFR32) expects every ASH DATA frame to be acknowledged within a few hundred ms,
// otherwise it retransmits and finally gives up with ASH ERROR 0x51 "ACK timeout" (ZbState Code 81).
// The Tasmota main loop can be blocked for 5-10 seconds while MQTT/TLS tries to reconnect
// (internet down but Wi-Fi up), so the ACKs were not sent in time.
// This high priority task reads the serial port, assembles ASH frames and sends the ACK immediately.
// Frames are then queued and processed by the main loop as before.
// Define ZIGBEE_EZSP_NO_RX_TASK to go back to the original behavior.
#if defined(ESP32) && !defined(ZIGBEE_EZSP_NO_RX_TASK)
#define ZIGBEE_EZSP_RX_TASK
#endif

#ifdef ZIGBEE_EZSP_RX_TASK
#ifndef ZIGBEE_RX_TASK_PRIORITY
#define ZIGBEE_RX_TASK_PRIORITY   5     // above loopTask (1), below Wi-Fi/lwIP
#endif
#ifndef ZIGBEE_RX_TASK_PERIOD_MS
#define ZIGBEE_RX_TASK_PERIOD_MS  5     // serial polling period, 256 bytes buffer holds ~22 ms at 115200 bps
#endif
const uint32_t ZIGBEE_RX_QUEUE_LEN = 64;      // frames waiting for the main loop
const uint32_t ZIGBEE_RX_NRDY_HIGH = 48;      // queue level: ask the NCP to hold its frames (ASH nRdy flag)
const uint32_t ZIGBEE_RX_NRDY_LOW = 16;       // queue level: tell the NCP we are ready again
const uint32_t ZIGBEE_RX_MIN_HEAP = 8192;     // don't allocate a frame below this free heap (treated as queue full)
const uint32_t ZIGBEE_RX_LATE_REPORT_MS = 1000; // log when the main loop picked a frame later than this

typedef struct {
  SBuffer *frame;
  uint32_t millis;          // when the frame was received and acknowledged
} ZbRxItem_t;

struct ZbRx_t {
  TaskHandle_t task = nullptr;
  QueueHandle_t queue = nullptr;
  SemaphoreHandle_t mutex = nullptr;   // recursive, protects all ZigbeeSerial and zigbee_buffer access
  volatile bool paused = false;        // true while the XMODEM uploader owns the serial port
  volatile uint32_t dropped = 0;       // frames lost because the queue was full (not acknowledged)
  volatile uint32_t bad_crc = 0;
  volatile uint32_t timeouts = 0;
  volatile uint32_t out_of_seq = 0;    // DATA frames refused after a drop, until the NCP retransmits the missing one
  volatile uint32_t nrdy_count = 0;    // times the NCP was asked to hold its frames
  bool reject = false;                 // a DATA frame was dropped: accept only the expected frame number (RX task only)
  bool nrdy = false;                   // last ACK sent with nRdy set (RX task only)
  uint32_t dropped_reported = 0;
  uint32_t out_of_seq_reported = 0;
  uint32_t nrdy_reported = 0;
  uint32_t bad_crc_reported = 0;
  uint32_t timeouts_reported = 0;
  uint32_t late_frames = 0;            // frames acknowledged by the task while the main loop was busy
  uint32_t late_max_ms = 0;
} ZbRx;

bool zigbee_frame_preacked = false;    // the frame being processed was already acknowledged by the RX task

inline bool ZigbeeInRxTask(void) {
  return (ZbRx.task != nullptr) && (xTaskGetCurrentTaskHandle() == ZbRx.task);
}

inline void ZigbeeSerialLock(void) {
  if (ZbRx.mutex) { xSemaphoreTakeRecursive(ZbRx.mutex, portMAX_DELAY); }
}

inline void ZigbeeSerialUnlock(void) {
  if (ZbRx.mutex) { xSemaphoreGiveRecursive(ZbRx.mutex); }
}

void ZigbeeRxTaskReport(void);
#endif // ZIGBEE_EZSP_RX_TASK

SBuffer * ZigbeeEZSPReadFrame(bool in_task);
void ZigbeeEZSPProcessFrame(SBuffer &ezsp_buffer);

#endif // USE_ZIGBEE_EZSP

//
// Blink Led Status
//
const uint32_t Z_LED_STATUS_ON_MILLIS = 40;   // keep led on at least 40 ms
bool Z_LedStatusSet(bool onoff) {
  static bool led_status_on = false;
  static uint32_t led_on_time = 0;

  if (onoff) {
    SetLedPowerIdx(ZIGBEE_LED_SERIAL, 1);
    led_status_on = true;
    led_on_time = millis();
  } else if ((led_status_on) && (TimePassedSince(led_on_time) >= Z_LED_STATUS_ON_MILLIS)) {
    SetLedPowerIdx(ZIGBEE_LED_SERIAL, 0);
    led_status_on = false;
  }
  return led_status_on;
}

#include <TasmotaSerial.h>
TasmotaSerial *ZigbeeSerial = nullptr;

/********************************************************************************************/
//
// Called at event loop, checks for incoming data from the CC2530
//
void ZigbeeInputLoop(void) {

#ifdef USE_ZIGBEE_ZNP
	static uint32_t zigbee_polling_window = 0;    // number of milliseconds since first byte
	static uint8_t fcs = ZIGBEE_SOF;
	static uint32_t zigbee_frame_len = 5;		      // minimal zigbee frame length, will be updated when buf[1] is read
  // Receive only valid ZNP frames:
  // 00 - SOF = 0xFE
  // 01 - Length of Data Field - 0..250
  // 02 - CMD1 - first byte of command
  // 03 - CMD2 - second byte of command
  // 04..FD - Data Field
  // FE (or last) - FCS Checksum

  Z_LedStatusSet(false);
  while (ZigbeeSerial->available()) {
    Z_LedStatusSet(true); // turn on receive LED<1>

    yield();
    uint8_t zigbee_in_byte = ZigbeeSerial->read();
		//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZbInput byte=%d len=%d"), zigbee_in_byte, zigbee_buffer->len());

		if (0 == zigbee_buffer->len()) {  // make sure all variables are correctly initialized
			zigbee_frame_len = 5;
			fcs = ZIGBEE_SOF;
      // there is a rare race condition when an interrupt occurs when receiving the first byte
      // in this case the first bit (lsb) is missed and Tasmota receives 0xFF instead of 0xFE
      // We forgive this mistake, and next bytes are automatically resynchronized
      if (ZIGBEE_SOF_ALT == zigbee_in_byte) {
        AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE "ZbInput forgiven first byte %02X (only for statistics)"), zigbee_in_byte);
        zigbee_in_byte = ZIGBEE_SOF;
      }
		}

    if ((0 == zigbee_buffer->len()) && (ZIGBEE_SOF != zigbee_in_byte)) {
      // waiting for SOF (Start Of Frame) byte, discard anything else
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE "ZbInput discarding byte %02X"), zigbee_in_byte);
      continue;     // discard
    }

    if (zigbee_buffer->len() < zigbee_frame_len) {
			zigbee_buffer->add8(zigbee_in_byte);
      zigbee_polling_window = millis();                               // Wait for more data
			fcs ^= zigbee_in_byte;
    }

		if (zigbee_buffer->len() >= zigbee_frame_len) {
      zigbee_polling_window = 0;                                      // Publish now
      break;
    }

    // recalculate frame length
    if (02 == zigbee_buffer->len()) {
      // We just received the Lenght byte
      uint8_t len_byte = zigbee_buffer->get8(1);
      if (len_byte > 250)  len_byte = 250;    // ZNP spec says len is 250 max

      zigbee_frame_len = len_byte + 5;        // SOF + LEN + CMD1 + CMD2 + FCS = 5 bytes overhead
    }
  }

  if (zigbee_buffer->len() && (millis() > (zigbee_polling_window + ZIGBEE_POLLING))) {
    // AddLog(LOG_LEVEL_DEBUG_MORE, PSTR(D_LOG_ZIGBEE "Bytes follow_read_metric = %0d"), ZigbeeSerial->getLoopReadMetric());
		// buffer received, now check integrity
		if (zigbee_buffer->len() != zigbee_frame_len) {
			// Len is not correct, log and reject frame
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE D_JSON_ZIGBEEZNPRECEIVED ": received frame of wrong size %_B, len %d, expected %d"), zigbee_buffer, zigbee_buffer->len(), zigbee_frame_len);
		} else if (0x00 != fcs) {
			// FCS is wrong, packet is corrupt, log and reject frame
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE D_JSON_ZIGBEEZNPRECEIVED ": received bad FCS frame %_B, %d"), zigbee_buffer, fcs);
		} else {
			// frame is correct
			//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR(D_JSON_ZIGBEEZNPRECEIVED ": received correct frame %s"), hex_char);

			SBuffer znp_buffer = zigbee_buffer->subBuffer(2, zigbee_frame_len - 3);	// remove SOF, LEN and FCS

      Response_P(PSTR("{\"" D_JSON_ZIGBEEZNPRECEIVED "\":\"%_B\"}"), &znp_buffer);
      if (Settings->flag3.tuya_serial_mqtt_publish) {
        MqttPublishPrefixTopicRulesProcess_P(TELE, PSTR(D_RSLT_SENSOR));
      } else {
        AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE "%s"), ResponseData());
      }
			// now process the message
      ZigbeeProcessInput(znp_buffer);
		}
		zigbee_buffer->setLen(0);		// empty buffer
  }
#endif // USE_ZIGBEE_ZNP

#ifdef USE_ZIGBEE_EZSP
  Z_LedStatusSet(false);

#ifdef ZIGBEE_EZSP_RX_TASK
  if (ZbRx.queue) {
    ZigbeeRxTaskReport();
    // process one frame already received and acknowledged by the RX task
    // only one per loop: the state machine must see each response before the next one
    ZbRxItem_t item;
    if (pdTRUE == xQueueReceive(ZbRx.queue, &item, 0)) {
      Z_LedStatusSet(true); // turn on receive LED<1>
      uint32_t waited = TimePassedSince(item.millis);
      if (waited > ZIGBEE_RX_LATE_REPORT_MS) {
        ZbRx.late_frames++;
        if (waited > ZbRx.late_max_ms) { ZbRx.late_max_ms = waited; }
      }
      zigbee_frame_preacked = true;
      ZigbeeEZSPProcessFrame(*item.frame);
      zigbee_frame_preacked = false;
      delete item.frame;
    }
    if (!ZbRx.paused) { return; }   // the RX task reads the serial port
  }
  // no RX task, or RX task paused: read the serial port from the main loop (original behavior)
  ZigbeeSerialLock();
#endif // ZIGBEE_EZSP_RX_TASK
  SBuffer *frame = ZigbeeEZSPReadFrame(false);
#ifdef ZIGBEE_EZSP_RX_TASK
  ZigbeeSerialUnlock();
#endif // ZIGBEE_EZSP_RX_TASK
  if (frame) {
    ZigbeeEZSPProcessFrame(*frame);
    delete frame;
  }
#endif // USE_ZIGBEE_EZSP

}

#ifdef USE_ZIGBEE_EZSP
// Read bytes from the serial port and assemble one ASH frame
// Returns a new SBuffer with the complete frame (CRC checked and removed, DATA field de-randomized)
// or nullptr if no complete frame is available yet. The caller must delete the buffer.
// in_task: called from the RX task, don't log nor touch the LED (main loop only)
SBuffer * ZigbeeEZSPReadFrame(bool in_task) {
	static uint32_t zigbee_polling_window = 0;    // number of milliseconds since first byte
  static bool escape = false;                          // was the previous byte an escape?
  bool frame_complete = false;                  // frame is ready and complete
  SBuffer *ezsp_buffer = nullptr;
  // Receive only valid EZSP frames:
  // 1A - Cancel - cancel all previous bytes
  // 7D - Escape byte - following byte is escaped
  // 7E - end of frame

  while (ZigbeeSerial->available()) {
    if (!in_task) {
      Z_LedStatusSet(true); // turn on receive LED<1>
      yield();
    }
    uint8_t zigbee_in_byte = ZigbeeSerial->read();
		// AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: ZbInput byte=0x%02X len=%d"), zigbee_in_byte, zigbee_buffer->len());

    if ((0x11 == zigbee_in_byte) || (0x13 == zigbee_in_byte)) {
      continue;           // ignore reserved bytes XON/XOFF
    }

    if (ZIGBEE_EZSP_ESCAPE == zigbee_in_byte) {
      // AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: Escape byte received"));
      escape = true;
      continue;
    }

    if (ZIGBEE_EZSP_CANCEL == zigbee_in_byte) {
      // AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: ZbInput byte=0x1A, cancel byte received, discarding %d bytes"), zigbee_buffer->len());
      zigbee_buffer->setLen(0);		// empty buffer
      escape = false;
      frame_complete = false;
      continue;                   // re-loop
    }

    if (ZIGBEE_EZSP_EOF == zigbee_in_byte) {
      // end of frame
      frame_complete = true;
      break;
    }

    if (zigbee_buffer->len() < ZIGBEE_BUFFER_SIZE) {
      if (escape) {
        // invert bit 5
        zigbee_in_byte ^= 0x20;
        escape = false;
      }

			zigbee_buffer->add8(zigbee_in_byte);
      zigbee_polling_window = millis();                               // Wait for more data
    }   // adding bytes
  }     // while (ZigbeeSerial->available())

  uint32_t frame_len = zigbee_buffer->len();
  if (frame_complete || (frame_len && (TimePassedSince(zigbee_polling_window) > ZIGBEE_POLLING))) {
    if ((frame_complete) && (frame_len >= 3)) {
      // frame received and has at least 3 bytes (without EOF), checking CRC
      uint16_t crc = 0xFFFF;                 // frame CRC
			// compute CRC
      for (uint32_t i=0; i<frame_len-2; i++) {
        crc = crc ^ ((uint16_t)zigbee_buffer->get8(i) << 8);
        for (uint32_t j=0; j<8; j++) {
          if (crc & 0x8000) {
            crc = (crc << 1) ^ 0x1021;          // polynom is x^16 + x^12 + x^5 + 1, CCITT standard
          } else {
            crc <<= 1;
          }
        }
      }

      uint16_t crc_received = zigbee_buffer->get8(frame_len - 2) << 8 | zigbee_buffer->get8(frame_len - 1);
      // remove 2 last bytes

      if (crc_received != crc) {
#ifdef ZIGBEE_EZSP_RX_TASK
        if (in_task) { ZbRx.bad_crc++; } else
#endif // ZIGBEE_EZSP_RX_TASK
        AddLog(LOG_LEVEL_INFO, PSTR(D_JSON_ZIGBEE_EZSP_RECEIVED ": bad crc (received 0x%04X, computed 0x%04X) %_B"), crc_received, crc, zigbee_buffer);
      } else {
        // copy buffer without CRC
#ifdef ZIGBEE_EZSP_RX_TASK
        if (in_task && (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < ZIGBEE_RX_MIN_HEAP)) {
          ZbRx.dropped++;                      // not enough memory, not acknowledged: the NCP will retransmit
          if (0 == (zigbee_buffer->get8(0) & 0x80)) { ZbRx.reject = true; }
          zigbee_buffer->setLen(0);
          escape = false;
          return nullptr;
        }
#endif // ZIGBEE_EZSP_RX_TASK
        ezsp_buffer = new SBuffer(frame_len - 2);
        ezsp_buffer->addBuffer(zigbee_buffer->getBuffer(), frame_len - 2);

        // CRC is correct, apply de-stuffing if DATA frame
        if (0 == (ezsp_buffer->get8(0) & 0x80)) {
          // DATA frame
          uint8_t rand = 0x42;
          for (uint32_t i=1; i<ezsp_buffer->len(); i++) {
            ezsp_buffer->set8(i, ezsp_buffer->get8(i) ^ rand);
            if (rand & 1) { rand = (rand >> 1) ^ 0xB8; }
            else          { rand = (rand >> 1); }
          }
        }
      }
    } else {
      // the buffer timed-out, print error and discard
#ifdef ZIGBEE_EZSP_RX_TASK
      if (in_task) { ZbRx.timeouts++; } else
#endif // ZIGBEE_EZSP_RX_TASK
      AddLog(LOG_LEVEL_INFO, PSTR(D_JSON_ZIGBEE_EZSP_RECEIVED ": time-out, discarding %_B"), zigbee_buffer);
    }
    zigbee_buffer->setLen(0);		// empty buffer
    escape = false;
    frame_complete = false;
  }
  return ezsp_buffer;
}

// Process a received ASH frame in the main loop
void ZigbeeEZSPProcessFrame(SBuffer &ezsp_buffer) {
  AddLog(LOG_LEVEL_DEBUG_MORE, PSTR(D_LOG_ZIGBEE "{\"" D_JSON_ZIGBEE_EZSP_RECEIVED "2\":\"%_B\"}"), &ezsp_buffer);
  // now process the message
  ZigbeeProcessInputRaw(ezsp_buffer);
}

#ifdef ZIGBEE_EZSP_RX_TASK
// RX task context: acknowledge a received frame right away
// ASH ACK byte: 1 0 0 0 nRdy ackNum(3)
// nRdy asks the NCP to hold its DATA frames while the queue is almost full,
// so the queue never overflows (the NCP resumes after an ACK without nRdy, or after ~1 s by itself)
void ZigbeeRxTaskSendAck(void) {
  uint32_t waiting = uxQueueMessagesWaiting(ZbRx.queue);
  if (!ZbRx.nrdy && (waiting >= ZIGBEE_RX_NRDY_HIGH)) {
    ZbRx.nrdy = true;
    ZbRx.nrdy_count++;
  } else if (ZbRx.nrdy && (waiting <= ZIGBEE_RX_NRDY_LOW)) {
    ZbRx.nrdy = false;
  }
  uint8_t ack_byte = 0x80 | (ZbRx.nrdy ? 0x08 : 0x00) | (EZSP_Serial.from_ack & 0x07);
  ZigbeeEZSPSendRaw(&ack_byte, 1, false);       // send a 1-byte ACK
}

void ZigbeeRxTaskAck(SBuffer &frame) {
  uint8_t control_byte = frame.get8(0);
  if (0 == (control_byte & 0x80)) {             // DATA frame
    EZSP_Serial.from_ack = ((control_byte >> 4) + 1) & 0x07;
    ZigbeeRxTaskSendAck();
  } else if (0xC1 == control_byte) {            // RSTACK, NCP restarted
    EZSP_Serial.from_ack = 0;
    ZbRx.reject = false;
    ZbRx.nrdy = false;
  }
}

// After a dropped DATA frame n, ASH acknowledgements are cumulative: acknowledging n+1 would tell the NCP
// that n was received too, and it would never retransmit it. So until frame n arrives again,
// later DATA frames are refused (not queued, not acknowledged) and the NCP retransmits from n.
bool ZigbeeRxTaskAccept(SBuffer &frame) {
  uint8_t control_byte = frame.get8(0);
  if (!ZbRx.reject || (control_byte & 0x80)) { return true; }      // normal case, or not a DATA frame
  uint8_t frm_num = (control_byte >> 4) & 0x07;
  return (frm_num == (EZSP_Serial.from_ack & 0x07));
}

void ZigbeeRxTask(void *) {
  const TickType_t period = (pdMS_TO_TICKS(ZIGBEE_RX_TASK_PERIOD_MS) > 0) ? pdMS_TO_TICKS(ZIGBEE_RX_TASK_PERIOD_MS) : 1;
  while (true) {
    if (!ZbRx.paused && ZigbeeSerial && zigbee_buffer) {
      ZigbeeSerialLock();
      if (!ZbRx.paused) {
        SBuffer *frame;
        while ((frame = ZigbeeEZSPReadFrame(true)) != nullptr) {
          if (!ZigbeeRxTaskAccept(*frame)) {
            delete frame;                       // out of sequence after a drop, the NCP will retransmit
            ZbRx.out_of_seq++;
            continue;
          }
          bool is_data = (0 == (frame->get8(0) & 0x80));
          ZbRxItem_t item = { frame, millis() };
          if (pdTRUE == xQueueSend(ZbRx.queue, &item, 0)) {
            if (is_data) { ZbRx.reject = false; }  // the expected frame arrived, back to normal
            ZigbeeRxTaskAck(*frame);            // acknowledge only frames we can process later
          } else {
            delete frame;                       // queue full, not acknowledged: the NCP will retransmit
            ZbRx.dropped++;
            if (is_data) { ZbRx.reject = true; }
          }
        }
        // queue drained after a nRdy: tell the NCP it can send again
        if (ZbRx.nrdy && (uxQueueMessagesWaiting(ZbRx.queue) <= ZIGBEE_RX_NRDY_LOW)) {
          ZigbeeRxTaskSendAck();
        }
      }
      ZigbeeSerialUnlock();
    }
    vTaskDelay(period);
  }
}

void ZigbeeRxTaskStart(void) {
  if (ZbRx.task) { return; }
  ZbRx.mutex = xSemaphoreCreateRecursiveMutex();
  ZbRx.queue = xQueueCreate(ZIGBEE_RX_QUEUE_LEN, sizeof(ZbRxItem_t));
  if (!ZbRx.mutex || !ZbRx.queue) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "RX task: out of memory, using main loop"));
    if (ZbRx.mutex) { vSemaphoreDelete(ZbRx.mutex); ZbRx.mutex = nullptr; }
    if (ZbRx.queue) { vQueueDelete(ZbRx.queue); ZbRx.queue = nullptr; }
    return;
  }
  // same core as the main loop: a higher priority preempts it even while it is busy with TLS
  if (pdPASS != xTaskCreatePinnedToCore(ZigbeeRxTask, "ZbRx", 4096, nullptr, ZIGBEE_RX_TASK_PRIORITY, &ZbRx.task, xPortGetCoreID())) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "RX task: failed to start, using main loop"));
    ZbRx.task = nullptr;
    vQueueDelete(ZbRx.queue);
    ZbRx.queue = nullptr;
    return;
  }
  AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "RX task started (priority %d, core %d)"), ZIGBEE_RX_TASK_PRIORITY, xPortGetCoreID());
}

// Main loop: report what happened in the RX task
void ZigbeeRxTaskReport(void) {
  uint32_t dropped = ZbRx.dropped;
  uint32_t bad_crc = ZbRx.bad_crc;
  uint32_t timeouts = ZbRx.timeouts;
  uint32_t out_of_seq = ZbRx.out_of_seq;
  uint32_t nrdy = ZbRx.nrdy_count;
  if ((dropped != ZbRx.dropped_reported) || (bad_crc != ZbRx.bad_crc_reported) || (timeouts != ZbRx.timeouts_reported) ||
      (out_of_seq != ZbRx.out_of_seq_reported) || (nrdy != ZbRx.nrdy_reported)) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "RX task: dropped %u, bad crc %u, time-out %u, out of seq %u, nRdy %u, stack free %u"),
                           dropped, bad_crc, timeouts, out_of_seq, nrdy, uxTaskGetStackHighWaterMark(ZbRx.task));
    ZbRx.dropped_reported = dropped;
    ZbRx.bad_crc_reported = bad_crc;
    ZbRx.timeouts_reported = timeouts;
    ZbRx.out_of_seq_reported = out_of_seq;
    ZbRx.nrdy_reported = nrdy;
  }
  // main loop is free again (queue empty): report how long frames waited
  if (ZbRx.late_frames && (0 == uxQueueMessagesWaiting(ZbRx.queue))) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "Main loop was busy, %u frames acknowledged by RX task, max wait %u ms"),
                           ZbRx.late_frames, ZbRx.late_max_ms);
    ZbRx.late_frames = 0;
    ZbRx.late_max_ms = 0;
  }
}
#endif // ZIGBEE_EZSP_RX_TASK
#endif // USE_ZIGBEE_EZSP

/********************************************************************************************/

// Initialize internal structures
void ZigbeeInitSerial(void)
{
  zigbee.active = false;

  // always initialize reset pins for TCP serial server
  if (PinUsed(GPIO_ZIGBEE_RST)) {
    pinMode(Pin(GPIO_ZIGBEE_RST), OUTPUT);
    digitalWrite(Pin(GPIO_ZIGBEE_RST), 1);
  }
  if (PinUsed(GPIO_ZIGBEE_RST, 1)) {
    pinMode(Pin(GPIO_ZIGBEE_RST, 1), OUTPUT);
    digitalWrite(Pin(GPIO_ZIGBEE_RST, 1), 1);
  }

  if (PinUsed(GPIO_ZIGBEE_RX) && PinUsed(GPIO_ZIGBEE_TX)) {
		AddLog(LOG_LEVEL_DEBUG_MORE, PSTR(D_LOG_ZIGBEE "GPIOs Rx:%d Tx:%d"), Pin(GPIO_ZIGBEE_RX), Pin(GPIO_ZIGBEE_TX));
    // if TasmotaGlobal.seriallog_level is 0, we allow GPIO 13/15 to switch to Hardware Serial
    int32_t uart_num = TasmotaGlobal.seriallog_level ? 1 : 2;
    ZigbeeSerial = new TasmotaSerial(Pin(GPIO_ZIGBEE_RX), Pin(GPIO_ZIGBEE_TX), uart_num, 0, 256);   // set a receive buffer of 256 bytes
    if (ZigbeeSerial == nullptr) {
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "Failed to allocate TasmotaSerial - aborting Zigbee"));
      return;
    }
    if (!ZigbeeSerial->isValid()) {
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_ZIGBEE "Invalid Serial GPIOs - aborting Zigbee"));
      delete ZigbeeSerial;
      ZigbeeSerial = nullptr;
      return;
    }
    ZigbeeSerial->begin(115200);
#ifdef ESP8266
    if (ZigbeeSerial->hardwareSerial()) {
      ClaimSerial();
		}
#endif // ESP8266
#ifdef ESP32
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE "Using Hardware Serial UART%d"), ZigbeeSerial->getUart());
#endif  // ESP32
    zigbee_buffer = new SBuffer(ZIGBEE_BUFFER_SIZE);
#if defined(USE_ZIGBEE_EZSP) && defined(ZIGBEE_EZSP_RX_TASK)
    ZigbeeRxTaskStart();
#endif

    zigbee.active = true;
		zigbee.init_phase = true;			// start the state machine
    zigbee.state_machine = true;      // start the state machine
    ZigbeeSerial->flush();
  }
}

#ifdef USE_ZIGBEE_ZNP

// flush any ongoing frame, sending 256 times 0xFF
void ZigbeeZNPFlush(void) {
  if (ZigbeeSerial) {
		for (uint32_t i = 0; i < 256; i++) {
			ZigbeeSerial->write(0xFF);
    }
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE D_JSON_ZIGBEEZNPSENT " 0xFF x 255"));
  }
}

void ZigbeeZNPSend(const uint8_t *msg, size_t len) {
	if ((len < 2) || (len > 252)) {
		// abort, message cannot be less than 2 bytes for CMD1 and CMD2
		AddLog(LOG_LEVEL_DEBUG, PSTR(D_JSON_ZIGBEEZNPSENT ": bad message len %d"), len);
		return;
	}
	uint8_t data_len = len - 2;		// removing CMD1 and CMD2

  if (ZigbeeSerial) {
    // turn send led on
    Z_LedStatusSet(true);

		uint8_t fcs = data_len;

		ZigbeeSerial->write(ZIGBEE_SOF);		// 0xFE
		//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZNPSend SOF %02X"), ZIGBEE_SOF);
		ZigbeeSerial->write(data_len);
		//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZNPSend LEN %02X"), data_len);
		for (uint32_t i = 0; i < len; i++) {
			uint8_t b = pgm_read_byte(msg + i);
			ZigbeeSerial->write(b);
			fcs ^= b;
			//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZNPSend byt %02X"), b);
		}
		ZigbeeSerial->write(fcs);			// finally send fcs checksum byte
		//AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZNPSend FCS %02X"), fcs);
  }
	// Now send a MQTT message to report the sent message
  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE D_JSON_ZIGBEEZNPSENT " %*_H"), len, msg);
}

//
// Same code for `ZbZNPSend` and `ZbZNPReceive`
// building the complete message (intro, length)
//
void CmndZbZNPSendOrReceive(bool send)
{
  if (ZigbeeSerial && (XdrvMailbox.data_len > 0)) {
    uint8_t code;

    char *codes = RemoveSpace(XdrvMailbox.data);
    int32_t size = strlen(XdrvMailbox.data);

		SBuffer buf((size+1)/2);

    while (size > 1) {
      char stemp[3];
      strlcpy(stemp, codes, sizeof(stemp));
      code = strtol(stemp, nullptr, 16);
			buf.add8(code);
      size -= 2;
      codes += 2;
    }
    if (send) {
      // Command was `ZbZNPSend`
      ZigbeeZNPSend(buf.getBuffer(), buf.len());
    } else {
      // Command was `ZbZNPReceive`
      ZigbeeProcessInput(buf);
    }
  }
  ResponseCmndDone();
}

// For debug purposes only, simulates a message received
void CmndZbZNPReceive(void)
{
  CmndZbZNPSendOrReceive(false);
}

void CmndZbZNPSend(void)
{
  CmndZbZNPSendOrReceive(true);
}

#endif // USE_ZIGBEE_ZNP

#ifdef USE_ZIGBEE_EZSP

// internal function to output a byte, and escape it (stuffing) if needed
void ZigbeeEZSPSend_Out(uint8_t out_byte) {
  switch (out_byte) {
    case 0x7E:      // Flag byte
    case 0x11:      // XON
    case 0x13:      // XOFF
    case 0x18:      // Substitute byte
    case 0x1A:      // Cancel byte
    case 0x7D:      // Escape byte
    // case 0xFF:      // special wake-up
      ZigbeeSerial->write(ZIGBEE_EZSP_ESCAPE);      // send Escape byte 0x7D
      ZigbeeSerial->write(out_byte ^ 0x20);           // send with bit 5 inverted
      break;
    default:
      ZigbeeSerial->write(out_byte);                  // send unchanged
      break;
  }
}
// Send low-level EZSP frames
//
// The frame should contain the Control Byte and Data Field
// The frame shouldn't be escaped, nor randomized
//
// Before sending:
// - send Cancel byte (0x1A) if requested
// - randomize Data Field if DATA Frame
// - compute CRC16
// - escape (stuff) reserved bytes
// - add EOF (0x7E)
// - send frame
// send_cancel: should we first send a EZSP_CANCEL (0x1A) before the message to clear any leftover
void ZigbeeEZSPSendRaw(const uint8_t *msg, size_t len, bool send_cancel) {
	if ((len < 1) || (len > 252)) {
		// abort, message cannot be less than 2 bytes for CMD1 and CMD2
		AddLog(LOG_LEVEL_DEBUG, PSTR(D_JSON_ZIGBEE_EZSP_SENT ": bad message len %d"), len);
		return;
	}
#ifdef ZIGBEE_EZSP_RX_TASK
  bool in_task = ZigbeeInRxTask();
  if (!in_task)
#endif // ZIGBEE_EZSP_RX_TASK
  // turn send led on
  Z_LedStatusSet(true);

  if (ZigbeeSerial) {
#ifdef ZIGBEE_EZSP_RX_TASK
    ZigbeeSerialLock();     // a frame must not be interleaved with an ACK sent by the RX task
#endif // ZIGBEE_EZSP_RX_TASK
    if (send_cancel) {
      ZigbeeSerial->write(ZIGBEE_EZSP_CANCEL);		// 0x1A
    }

    bool data_frame = (0 == (msg[0] & 0x80));
    uint8_t rand = 0x42;          // pseudo-randomizer initial value
    uint16_t crc = 0xFFFF;        // CRC16 CCITT initialization

    for (uint32_t i=0; i<len; i++) {
      uint8_t out_byte = msg[i];

      // apply randomization if DATA field
      if (data_frame && (i > 0)) {
        out_byte ^= rand;
        if (rand & 1) { rand = (rand >> 1) ^ 0xB8; }
        else          { rand = (rand >> 1); }
      }

      // compute CRC
      crc = crc ^ ((uint16_t)out_byte << 8);
      for (uint32_t j=0; j<8; j++) {
        if (crc & 0x8000) {
          crc = (crc << 1) ^ 0x1021;          // polynom is x^16 + x^12 + x^5 + 1, CCITT standard
        } else {
          crc <<= 1;
        }
      }

      // output byte
      ZigbeeEZSPSend_Out(out_byte);
    }
    // send CRC16 in big-endian
    ZigbeeEZSPSend_Out(crc >> 8);
    ZigbeeEZSPSend_Out(crc & 0xFF);

    // finally send End of Frame
    ZigbeeSerial->write(ZIGBEE_EZSP_EOF);		// 0x1A
#ifdef ZIGBEE_EZSP_RX_TASK
    ZigbeeSerialUnlock();
#endif // ZIGBEE_EZSP_RX_TASK
  }

#ifdef ZIGBEE_EZSP_RX_TASK
  if (in_task) { return; }
#endif // ZIGBEE_EZSP_RX_TASK
  // Now send a MQTT message to report the sent message
  AddLog(LOG_LEVEL_DEBUG_MORE, PSTR(D_LOG_ZIGBEE D_JSON_ZIGBEE_EZSP_SENT_RAW " %*_H"), len, msg);
}

// Send an EZSP command and data
// Ex: Version with min v8 = 000008
void ZigbeeEZSPSendCmd(const uint8_t *msg, size_t len) {
  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_ZIGBEE "ZbEZSPSend %*_H"), len, msg);

  SBuffer cmd(len+3);   // prefix with seq number (1 byte) and frame control bytes (2 bytes)

  cmd.add8(EZSP_Serial.ezsp_seq++);
  cmd.add8(0x00);       // Low byte of Frame Control
  cmd.add8(0x01);       // High byte of Frame Control, frameFormatVersion = 1
  cmd.addBuffer(msg, len);

  // send
  ZigbeeEZSPSendDATA(cmd.getBuffer(), cmd.len());
}

// Send an EZSP DATA frame, automatically calculating the correct frame numbers
void ZigbeeEZSPSendDATA_frm(bool send_cancel, uint8_t to_frm, uint8_t from_ack) {
  SBuffer *buf = EZSP_Serial.to_packets[to_frm];
  if (!buf) {
    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: Buffer for packet %d is not allocated"), EZSP_Serial.to_send);
    return;
  }

  uint8_t control_byte = ((to_frm & 0x07) << 4) + (from_ack & 0x07);
  buf->set8(0, control_byte);      // change control_byte
  // send
  ZigbeeEZSPSendRaw(buf->getBuffer(), buf->len(), send_cancel);
}

// Send an EZSP DATA frame, automatically calculating the correct frame numbers
void ZigbeeEZSPSendDATA(const uint8_t *msg, size_t len) {
  // prepare buffer by adding 1 byte prefix
  SBuffer *buf = new SBuffer(len+1);    // prepare for control_byte prefix
  buf->add8(0x00);                       // placeholder for control_byte
  buf->addBuffer(msg, len);
  //
  AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: adding packet to_send, to_ack:%d, to_send:%d, to_end:%d"),
                                  EZSP_Serial.to_ack, EZSP_Serial.to_send, EZSP_Serial.to_end);
  uint8_t to_frm = EZSP_Serial.to_end;
  if (EZSP_Serial.to_packets[to_frm]) {
    delete EZSP_Serial.to_packets[to_frm];
    EZSP_Serial.to_packets[to_frm] = nullptr;
  }
  EZSP_Serial.to_packets[to_frm] = buf;
  EZSP_Serial.to_end = (to_frm + 1) & 0x07;   // move cursor

  // ZigbeeEZSPSendDATA_frm(send_cancel, to_frm, EZSP_Serial.from_ack);

  // increment to_frame
  //EZSP_Serial.to_ack = (EZSP_Serial.to_ack + 1) & 0x07;
  //EZSP_Serial.to_frm = (EZSP_Serial.to_frm + 1) & 0x07;
}

// Receive a high-level EZSP command/response, starting with 16-bits frame ID
void ZigbeeProcessInputEZSP(SBuffer &buf) {
  // verify errors in first 2 bytes.
  // TODO
  // uint8_t  sequence_num = buf.get8(0);
  uint16_t frame_control = buf.get16(1);
  bool truncated = frame_control & 0x02;
  bool overflow = frame_control & 0x01;
  // bool callbackPending = frame_control & 0x04;
  bool security_enabled = frame_control & 0x8000;
  if (truncated || overflow || security_enabled) {
    AddLog(LOG_LEVEL_INFO, PSTR("ZIG: specific frame_control 0x%04X"), frame_control);
  }

  // remove first 2 bytes, be
  for (uint32_t i=0; i<buf.len()-3; i++) {
    buf.set8(i, buf.get8(i+3));
  }
  buf.setLen(buf.len() - 3);

  // log message
  Response_P(PSTR("{\"" D_JSON_ZIGBEE_EZSP_RECEIVED "\":\"%_B\"}"), &buf);
  if (Settings->flag3.tuya_serial_mqtt_publish) {
    MqttPublishPrefixTopicRulesProcess_P(TELE, PSTR(D_RSLT_SENSOR));
  } else {
    // demote less interesting messages to LOG_LEVEL_DEBUG
    uint32_t log_level = LOG_LEVEL_INFO;
    switch (buf.get16(0)) {
      case EZSP_version:                  // 0000
      case EZSP_addEndpoint:              // 0200
      case EZSP_setConcentrator:          // 1000
      case EZSP_networkInit:              // 1700
      case EZSP_stackStatusHandler:       // 1900
      case EZSP_startScan:                // 1A00
      case EZSP_scanCompleteHandler:      // 1C00
      case EZSP_formNetwork:              // 1E00
      case EZSP_permitJoining:            // 2200
      case EZSP_getEui64:                 // 2600
      case EZSP_getNodeId:                // 2700
      case EZSP_getNetworkParameters:     // 2800
      case EZSP_sendUnicast:              // 3400
      case EZSP_sendBroadcast:            // 3600
      case EZSP_sendMulticast:            // 3800
      case EZSP_messageSentHandler:       // 3F00
      case EZSP_incomingMessageHandler:   // 4500
      case EZSP_energyScanResultHandler:  // 4800
      case EZSP_setConfigurationValue:    // 5300
      case EZSP_setPolicy:                // 5500
      case 0x0059:                        // 5900 - supposedly removed by still happening
      case EZSP_setMulticastTableEntry:   // 6400
      case EZSP_setInitialSecurityState:  // 6800
      case EZSP_getCurrentSecurityState:  // 6900
      case EZSP_getKey:                   // 6A00
        log_level = LOG_LEVEL_DEBUG;
        break;
    }
    AddLog(log_level, PSTR(D_LOG_ZIGBEE "%s"), ResponseData());    // TODO move to LOG_LEVEL_DEBUG when stable
  }

  // Pass message to state machine
  ZigbeeProcessInput(buf);
}

// Check if we advanced in the ACKed frames, and free from memory packets acknowledged
void EZSP_HandleAck(uint8_t new_ack) {
  if (EZSP_Serial.to_ack != new_ack) {      // new ack receveid
    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: new ack/data received, was %d now %d"), EZSP_Serial.to_ack, new_ack);
    uint32_t i = EZSP_Serial.to_ack;
    do {
      if (EZSP_Serial.to_packets[i]) {
        delete EZSP_Serial.to_packets[i];
        EZSP_Serial.to_packets[i] = nullptr;
      }
      AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: freeing packet %d from memory"), i);
      i = (i + 1) & 0x07;
    } while (i != new_ack);
    EZSP_Serial.to_ack = new_ack;
  }
}

// Receive raw ASH frame (CRC was removed, data unstuffed) but still contains frame numbers
void ZigbeeProcessInputRaw(SBuffer &buf) {
  uint8_t control_byte = buf.get8(0);
  uint8_t ack_num = control_byte & 0x07;        // keep 3 LSB
  if (control_byte & 0x80) {  // non DATA frame

    uint8_t frame_type = control_byte & 0xE0;   // keep 3 MSB
    if (frame_type == 0x80) {

      // ACK
      EZSP_HandleAck(ack_num);
    } else if (frame_type == 0xA0) {

      // NAK
      AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: Received NAK %d, to_ack:%d, to_send:%d, to_end:%d"),
                                  ack_num, EZSP_Serial.to_ack, EZSP_Serial.to_send, EZSP_Serial.to_end);
      EZSP_Serial.to_send = ack_num;
      AddLog(LOG_LEVEL_DEBUG, PSTR("ZIG: NAK, resending packet %d"), ack_num);
    } else if (control_byte == 0xC1) {

      // RSTACK
      // received just after boot, either because of Power up, hardware reset or RST
      EZ_RSTACK(buf.get8(2));
      EZSP_Serial.from_ack = 0;
      EZSP_Serial.to_ack = 0;
      EZSP_Serial.to_end = 0;
      EZSP_Serial.to_send = 0;

      // pass it to state machine with a special 0xFFFE frame code (EZSP_RSTACK_ID)
      buf.set8(0, Z_B0(EZSP_rstAck));
      buf.set8(1, Z_B1(EZSP_rstAck));
      // keep byte #2 with code
      buf.setLen(3);
      ZigbeeProcessInput(buf);
    } else if (control_byte == 0xC2) {

      // ERROR
      EZ_ERROR(buf.get8(2));
      zigbee.active = false;           // stop all zigbee activities
      TasmotaGlobal.restart_flag = 2;  // there is nothing more we can do except restart
    } else {

      // Unknown
      AddLog(LOG_LEVEL_DEBUG, PSTR("ZIG: Received unknown control byte 0x%02X"), control_byte);
    }
  } else {    // DATA Frame

    // adjust to latest acked packet
    uint8_t new_ack = control_byte & 0x07;
    EZSP_HandleAck(new_ack);

    // MCU acknowledged the correct frame
    // we acknowledge the frame too
#ifdef ZIGBEE_EZSP_RX_TASK
    if (!zigbee_frame_preacked)     // otherwise the RX task already sent the ACK
#endif // ZIGBEE_EZSP_RX_TASK
    {
      EZSP_Serial.from_ack = ((control_byte >> 4) + 1) & 0x07;
      uint8_t ack_byte = 0x80 | EZSP_Serial.from_ack;
      ZigbeeEZSPSendRaw(&ack_byte, 1, false);   // send a 1-byte ACK
    }

    // build the EZSP frame
    // remove first byte
    for (uint8_t i=0; i<buf.len()-1; i++) {
      buf.set8(i, buf.get8(i+1));
    }
    buf.setLen(buf.len()-1);
    // pass to next level
    ZigbeeProcessInputEZSP(buf);
  }
}

//
// Same code for `ZbEZSPSend` and `ZbEZSPReceive`
// building the complete message (intro, length)
//
// ZbEZSPSend1 = high level EZSP command
// ZbEZSPSend2 = low level EZSP DATA frame (with sequence numbers)
// ZbEZSPSend3 = low level ASH frame
//
void CmndZbEZSPSendOrReceive(bool send)
{
  if (ZigbeeSerial && (XdrvMailbox.data_len > 0)) {
    uint8_t code;

    char *codes = RemoveSpace(XdrvMailbox.data);
    int32_t size = strlen(XdrvMailbox.data);

		SBuffer buf((size+1)/2);

    while (size > 1) {
      char stemp[3];
      strlcpy(stemp, codes, sizeof(stemp));
      code = strtol(stemp, nullptr, 16);
			buf.add8(code);
      size -= 2;
      codes += 2;
    }
    if (send) {
      // Command was `ZbEZSPSend`
      if      (2 == XdrvMailbox.index) { ZigbeeEZSPSendDATA(buf.getBuffer(), buf.len()); }
      else if (3 == XdrvMailbox.index) { ZigbeeEZSPSendRaw(buf.getBuffer(), buf.len(), true); }
      else                             { ZigbeeEZSPSendCmd(buf.getBuffer(), buf.len()); }

    } else {
      // Command was `ZbEZSPReceive`
      if      (2 == XdrvMailbox.index) { ZigbeeProcessInput(buf); }
      else if (3 == XdrvMailbox.index) { ZigbeeProcessInputRaw(buf); }
      else                             { ZigbeeProcessInputEZSP(buf); }   // TODO
    }
  }
  ResponseCmndDone();
}
// Variants with managed ASH frame numbers
// For debug purposes only, simulates a message received
void CmndZbEZSPReceive(void)
{
  CmndZbEZSPSendOrReceive(false);
}

void CmndZbEZSPSend(void)
{
  CmndZbEZSPSendOrReceive(true);
}
#endif // USE_ZIGBEE_EZSP

//
// Internal function, send the low-level frame
// Input:
// - shortaddr: 16-bits short address, or 0x0000 if group address
// - groupaddr: 16-bits group address, or 0x0000 if unicast using shortaddr
// - clusterIf: 16-bits cluster number
// - endpoint:  8-bits target endpoint (source is always 0x01), unused for group addresses. Should not be 0x00 except when sending to group address.
// - cmd:     8-bits ZCL command number
// - clusterSpecific: boolean, is the message general cluster or cluster specific, used to create the FC byte of ZCL
// - msg:       pointer to byte array, payload of ZCL message (len is following), ignored if nullptr
// - len:       length of the 'msg' payload
// - needResponse: boolean, true = we ask the target to respond, false = the target should not respond
// - transactseq: 8-bits, transation id of message (should be incremented at each message), used both for Zigbee message number and ZCL message number
// Returns: None
//

void ZigbeeZCLSend_Raw(const ZCLFrame &zcl) {
  SBuffer buf(32+zcl.payload.len());

#ifdef USE_ZIGBEE_ZNP
  buf.add8(Z_SREQ | Z_AF);          // 24
  buf.add8(AF_DATA_REQUEST_EXT);    // 02
  if (!zcl.validShortaddr()) {        // if no shortaddr we assume group address
    buf.add8(Z_Addr_Group);         // 01
    buf.add64(zcl.groupaddr);           // group address, only 2 LSB, upper 6 MSB are discarded
    buf.add8(0xFF);                 // dest endpoint is not used for group addresses
  } else {
    buf.add8(Z_Addr_ShortAddress);  // 02
    buf.add64(zcl.shortaddr);           // dest address, only 2 LSB, upper 6 MSB are discarded
    buf.add8(zcl.dstendpoint);             // dest endpoint
  }
  buf.add16(0x0000);                // dest Pan ID, 0x0000 = intra-pan
  if (zcl.srcendpoint) {
    buf.add8(zcl.srcendpoint);        // source endpoint
  } else {
    buf.add8(1);                      // set endpoint to 1 if not specified
  }
  buf.add16(zcl.cluster);
  buf.add8(zcl.transactseq);              // transactseq
  buf.add8(zcl.direct ? 0x00 : 0x30);                   // 30 options
  buf.add8(0x1E);                   // 1E radius

  buf.add16(3 + zcl.payload.len() + (zcl.manuf ? 2 : 0));
  buf.add8((zcl.needResponse ? 0x00 : 0x10) | (zcl.clusterSpecific ? 0x01 : 0x00) |
           (zcl.manuf ? 0x04 : 0x00) | (zcl.direction ? 0x08 : 0x00));                 // Frame Control Field
  if (zcl.manuf) {
    buf.add16(zcl.manuf);               // add Manuf Id if not null
  }
  buf.add8(zcl.transactseq);              // Transaction Sequence Number
  buf.add8(zcl.cmd);
  buf.addBuffer(zcl.payload);

  ZigbeeZNPSend(buf.getBuffer(), buf.len());
#endif // USE_ZIGBEE_ZNP

#ifdef USE_ZIGBEE_EZSP
  if (zcl.validShortaddr()) {
    // send unicast message to an address
    buf.add16(EZSP_sendUnicast);          // 3400
    buf.add8(EMBER_OUTGOING_DIRECT);    // 00
    buf.add16(zcl.shortaddr);               // dest addr
    // ApsFrame
    buf.add16(Z_PROF_HA);               // Home Automation profile
    buf.add16(zcl.cluster);               // cluster
    buf.add8(0x01);                     // srcEp
    buf.add8(zcl.dstendpoint);                 // dstEp
    if (zcl.direct) {
      buf.add16(0x0000);      // APS frame
    } else {
      buf.add16(EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_RETRY);      // APS frame
    }
    buf.add16(zcl.groupaddr);               // groupId
    buf.add8(zcl.transactseq);
    // end of ApsFrame
    buf.add8(0x01);                     // tag TODO

    buf.add8(3 + zcl.payload.len() + (zcl.manuf ? 2 : 0));
    buf.add8((zcl.needResponse ? 0x00 : 0x10) | (zcl.clusterSpecific ? 0x01 : 0x00) | (zcl.manuf ? 0x04 : 0x00));                 // Frame Control Field
    if (zcl.manuf) {
      buf.add16(zcl.manuf);               // add Manuf Id if not null
    }
    buf.add8(zcl.transactseq);              // Transaction Sequance Number
    buf.add8(zcl.cmd);
    buf.addBuffer(zcl.payload);
  } else {
    // send broadcast group address, aka groupcast
    buf.add16(EZSP_sendMulticast);      // 3800
    // ApsFrame
    buf.add16(Z_PROF_HA);               // Home Automation profile
    buf.add16(zcl.cluster);               // cluster
    buf.add8(0x01);                     // srcEp
    buf.add8(zcl.dstendpoint);                 // broadcast endpoint for groupcast
    if (zcl.direct) {
      buf.add16(0x0000);      // APS frame
    } else {
      buf.add16(EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_RETRY);      // APS frame
    }
    buf.add16(zcl.groupaddr);               // groupId
    buf.add8(zcl.transactseq);
    // end of ApsFrame
    buf.add8(0);                        // hops, 0x00 = EMBER_MAX_HOPS
    buf.add8(7);                        // nonMemberRadius, 7 = infinite
    buf.add8(0x01);                     // tag TODO

    buf.add8(3 + zcl.payload.len() + (zcl.manuf ? 2 : 0));
    buf.add8((zcl.needResponse ? 0x00 : 0x10) | (zcl.clusterSpecific ? 0x01 : 0x00) | (zcl.manuf ? 0x04 : 0x00));                 // Frame Control Field
    if (zcl.manuf) {
      buf.add16(zcl.manuf);               // add Manuf Id if not null
    }
    buf.add8(zcl.transactseq);              // Transaction Sequance Number
    buf.add8(zcl.cmd);
    buf.addBuffer(zcl.payload);
  }

  ZigbeeEZSPSendCmd(buf.buf(), buf.len());
#endif // USE_ZIGBEE_EZSP
}

//
// Send any buffered data to the NCP
//
// Used only with EZSP, as there is no replay of procotol control with ZNP
void ZigbeeOutputLoop(void) {
#ifdef USE_ZIGBEE_EZSP
  // while (EZSP_Serial.to_send != EZSP_Serial.to_end) {
  if (EZSP_Serial.to_send != EZSP_Serial.to_end) {   // we send only one packet per tick to lower the chance of NAK
    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("ZIG: Something to_send, to_ack:%d, to_send:%d, to_end:%d"),
                                  EZSP_Serial.to_ack, EZSP_Serial.to_send, EZSP_Serial.to_end);
    // we have a frame waiting to be sent
    ZigbeeEZSPSendDATA_frm(true, EZSP_Serial.to_send, EZSP_Serial.from_ack);
    // increment sent counter
    EZSP_Serial.to_send = (EZSP_Serial.to_send + 1) & 0x07;
  }
#endif // USE_ZIGBEE_EZSP
}

#endif // USE_ZIGBEE
