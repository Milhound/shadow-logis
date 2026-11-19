#pragma once

class QApplication;

namespace shadowlogis {
// App-local fonts and palette-derived widget styling. Does not install fonts,
// change the desktop palette, or change any PDF renderer's typography.
void installAppearance(QApplication &app);
}
