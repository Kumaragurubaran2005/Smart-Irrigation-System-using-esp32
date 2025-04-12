

@echo off
echo Starting Node.js server...
start /B node server.js

echo Starting Python API/ML server...
start /B python app_api_integrated.py

echo Launching frontend...
start home.html

echo All services started successfully!
