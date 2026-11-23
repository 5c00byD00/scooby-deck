#if defined(SCOOBY_DECK)
// DualMesh glue: things the dual-mesh code needs from WadaMesh.
#include <Arduino.h>
#include "../MyMesh.h"
#include "DualMeshMt.h"

namespace DualMesh {
const char* wadaNodeName() {
    NodePrefs* p = the_mesh.getNodePrefs();
    return p ? p->node_name : "";
}
}
#endif  // SCOOBY_DECK
