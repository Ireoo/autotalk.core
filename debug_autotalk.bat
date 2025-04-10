@echo off
echo ===== AutoTalk调试批处理 =====
echo 正在检查环境...

echo 1. 检查Release目录...
if not exist Release (
  echo 错误: Release目录不存在
  pause
  exit /b 1
)

echo 2. 检查可执行文件...
if not exist Release\autotalk.exe (
  echo 错误: autotalk.exe不存在
  pause
  exit /b 1
)

echo 3. 检查所需DLL文件...
set MISSING_DLL=0
if not exist Release\whisper.dll (
  echo 错误: whisper.dll不存在
  set MISSING_DLL=1
)
if not exist Release\ggml.dll (
  echo 错误: ggml.dll不存在
  set MISSING_DLL=1
)
if not exist Release\ggml-base.dll (
  echo 错误: ggml-base.dll不存在
  set MISSING_DLL=1
)
if not exist Release\ggml-cpu.dll (
  echo 错误: ggml-cpu.dll不存在
  set MISSING_DLL=1
)
if not exist Release\portaudio.dll (
  echo 错误: portaudio.dll不存在
  set MISSING_DLL=1
)

if %MISSING_DLL%==1 (
  echo 缺少必要的DLL文件，程序无法运行
  pause
  exit /b 1
)

echo 4. 检查模型文件...
set MODEL_PATH=models\ggml-small.bin
if not exist %MODEL_PATH% (
  echo 模型文件 %MODEL_PATH% 不存在，将尝试使用tiny模型
  set MODEL_PATH=models\ggml-tiny.bin
  
  if not exist %MODEL_PATH% (
    echo 错误: 找不到任何可用的模型文件
    pause
    exit /b 1
  )
)

echo 5. 运行程序...
echo 将使用模型: %MODEL_PATH%
echo 如果程序在运行中崩溃，请检查autotalk_debug.log文件获取更多信息

cd Release
autotalk.exe ..\%MODEL_PATH% > ..\autotalk_output.log 2>&1

echo 6. 程序退出，检查输出日志...
if exist ..\autotalk_output.log (
  echo 输出日志已保存到autotalk_output.log
) else (
  echo 警告: 未能创建输出日志
)

cd ..
echo ===== 调试完成 =====
pause 