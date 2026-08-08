import json
import logging
from channels.generic.websocket import AsyncWebsocketConsumer

logger = logging.getLogger(__name__)

class SensorConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        self.room_group_name = 'sensor_data'
        try:
            await self.channel_layer.group_add(self.room_group_name, self.channel_name)
            await self.accept()
        except Exception as e:
            logger.warning(f"WebSocket Connect Warning: {e}")
            try:
                await self.accept()
            except Exception:
                pass

    async def disconnect(self, close_code):
        try:
            await self.channel_layer.group_discard(self.room_group_name, self.channel_name)
        except Exception:
            pass

    async def send_sensor_data(self, event):
        try:
            data = event.get('data', {})
            await self.send(text_data=json.dumps(data))
        except Exception as e:
            logger.debug(f"Sensor send failed: {e}")

    async def send_relay_data(self, event):
        try:
            data = event.get('data', {})
            await self.send(text_data=json.dumps(data))
        except Exception as e:
            logger.debug(f"Relay send failed: {e}")