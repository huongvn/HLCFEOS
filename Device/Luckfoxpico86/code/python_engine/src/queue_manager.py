"""
Queue Manager - hàng đợi cho từng gateway (gửi + nhận)
Tránh mất lệnh điều khiển và dữ liệu từ gateway.
"""

import json
import logging
import threading
from queue import Queue, Empty
from typing import Any, Dict, Callable, Optional

logger = logging.getLogger(__name__)


class QueueManager:
    """
    Quản lý hàng đợi cho mỗi gateway.
    - out_queue: lệnh ZbSend gửi đến gateway
    - in_queue:  dữ liệu SENSOR nhận từ gateway
    """

    def __init__(self, mqtt_client: Any):
        self.mqtt_client = mqtt_client
        self.out_queue: Queue = Queue()
        self.in_queue: Queue = Queue()
        self.running = False
        self._out_thread: Optional[threading.Thread] = None
        self._in_thread: Optional[threading.Thread] = None
        self._process_callback: Optional[Callable] = None

    def set_process_callback(self, callback: Callable):
        """
        Set callback để xử lý dữ liệu SENSOR nhận được.
        Callback signature: (topic: str, payload: Any) -> None
        """
        self._process_callback = callback

    def start(self):
        """Start worker threads"""
        self.running = True
        self._out_thread = threading.Thread(target=self._worker_out,
                                            daemon=True, name="QueueOut")
        self._in_thread = threading.Thread(target=self._worker_in,
                                           daemon=True, name="QueueIn")
        self._out_thread.start()
        self._in_thread.start()
        logger.info("Queue Manager started (in + out workers)")

    def stop(self):
        """Stop worker threads"""
        self.running = False
        if self._out_thread and self._out_thread.is_alive():
            self._out_thread.join(timeout=3)
        if self._in_thread and self._in_thread.is_alive():
            self._in_thread.join(timeout=3)
        logger.info("Queue Manager stopped")

    # ── Outgoing ──

    def send_zbsend(self, gateway: str, zigbee_addr: str,
                    write_dict: Dict, endpoint: Optional[int] = None):
        """
        Xếp lệnh ZbSend vào hàng đợi gửi đi.
        Không bao giờ mất - queue lưu trong RAM, retry nếu fail.
        """
        payload = {"Device": zigbee_addr, "Write": write_dict}
        if endpoint is not None:
            payload["Endpoint"] = endpoint
        self.out_queue.put((gateway, payload))
        logger.debug(f"OUT QUEUED: {gateway} → {zigbee_addr} {write_dict}")

    def _worker_out(self):
        """Worker thread xử lý hàng đợi gửi đi"""
        while self.running:
            try:
                gateway, payload = self.out_queue.get(timeout=1)
                topic = f"cmnd/{gateway}/ZbSend"
                self.mqtt_client.publish(topic, json.dumps(payload), qos=1)
                logger.info(f"OUT SENT: {topic} = {payload}")
            except Empty:
                continue
            except Exception as e:
                logger.error(f"OUT error: {e}")
                # Re-queue on failure
                try:
                    self.out_queue.put((gateway, payload))
                except Exception:
                    pass

    # ── Incoming ──

    def enqueue_sensor(self, topic: str, payload: Any):
        """
        Xếp dữ liệu SENSOR vào hàng đợi xử lý.
        """
        self.in_queue.put((topic, payload))

    def _worker_in(self):
        """Worker thread xử lý hàng đợi nhận về"""
        while self.running:
            try:
                topic, payload = self.in_queue.get(timeout=1)
                if self._process_callback:
                    try:
                        self._process_callback(topic, payload)
                    except Exception as e:
                        logger.error(f"IN process error: {e}")
            except Empty:
                continue
            except Exception as e:
                logger.error(f"IN error: {e}")
