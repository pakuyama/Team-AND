@echo off
chcp 65001 > nul
cd /d "%~dp0"
rem ESP32 손 카메라 주소 (ESP32 시리얼 모니터에 나오는 주소로 바꾸세요)
set HANDCAM=http://192.168.0.20:81/stream
start "" http://localhost:8000
python web_server.py --camera %HANDCAM% %*
pause
