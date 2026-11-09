#pragma once

namespace crashHandler
{
// Install before constructing QApplication so startup failures are logged as well.
void initialize();
}