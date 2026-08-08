import json
from channels.generic.websocket import AsyncWebsocketConsumer

class SensorConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        self.room_group_name = 'sensor_data'
        await self.channel_layer.group_add(self.room_group_name, self.channel_name)
        await self.accept()

    async def disconnect(self, close_code):
        await self.channel_layer.group_discard(self.room_group_name, self.channel_name)

    async def send_sensor_data(self, event):
        data = event['data']
        await self.send(text_data=json.dumps(data))

    async def send_relay_data(self, event):
        data = event['data']
        await self.send(text_data=json.dumps(data))