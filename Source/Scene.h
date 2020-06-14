#pragma once

#include <functional>

void gCreateScene();
void gCleanupScene();
void gUpdateScene();
void gRebuildBinding(std::function<void ()> inCallback);