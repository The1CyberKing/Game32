#pragma once
struct SDManager {
    static SDManager& getInstance() { static SDManager s; return s; }
};
