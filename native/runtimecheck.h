#pragma once
class QApplication;
namespace shadowlogis {
// Synthetic checks only: never open a workspace or leave document files behind.
void checkRuntime(QApplication &app);
}
