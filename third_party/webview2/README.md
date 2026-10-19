# Microsoft Edge WebView2 Loader

`include/WebView2.h` and `x64/WebView2Loader.dll` come from the NuGet package
`Microsoft.Web.WebView2` 1.0.2849.39 (BSD-style license in `LICENSE.txt`).

Helios загружает DLL рядом с `helios.exe` и показывает в окне тот же React-интерфейс,
что и в браузере. На машине должен быть установлен WebView2 Runtime (обычно уже есть
вместе с Microsoft Edge).
