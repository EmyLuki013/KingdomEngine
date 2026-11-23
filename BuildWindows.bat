@echo off

REM COMPILANDO BIBLIOTECAS
call tools\Windows\KingBuilder.exe -relative ThirdParty\ImGui\

REM COMPILANDO BIBLIOTECA DA ENGINE E O EDITOR
call tools\Windows\KingBuilder.exe -relative KingdomEngine\
call tools\Windows\KingBuilder.exe -relative KingdomEditor\

pause