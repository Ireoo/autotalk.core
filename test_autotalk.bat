@echo off
echo ===== AutoTalk测试脚本 =====
echo 路径: %CD%

cd Release
echo 当前目录: %CD%

echo 检查模型文件...
set MODEL=..\models\ggml-tiny.bin
if exist %MODEL% (
  echo 模型文件: %MODEL% 存在
) else (
  echo 错误: 模型文件 %MODEL% 不存在!
  goto :eof
)

echo 运行程序...
autotalk.exe %MODEL%

echo 程序退出
pause 