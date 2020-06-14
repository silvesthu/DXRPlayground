#pragma once

void gCreateVertexBuffer();
void gCleanupVertexBuffer();

void gCreateBottomLevelAccelerationStructure();
void gCleanupBottomLevelAccelerationStructure();

void gCreateTopLevelAccelerationStructure();
void gUpdateTopLevelAccelerationStructure();
void gCleanupTopLevelAccelerationStructure();
void gExecuteAccelerationStructureCreation();