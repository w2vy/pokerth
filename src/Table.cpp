#include "Table.h"

const char* stateName(TableState state) {
    auto it = stateNameMap.find(state);
    return it!= stateNameMap.end()? it->second : "Unknown";
}
