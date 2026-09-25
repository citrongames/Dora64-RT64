//
// RT64
//

#include "rt64_gbi_f3d.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "../include/rt64_extended_gbi.h"

#include "rt64_f3d.h"
#include "rt64_gbi_extended.h"
#include "rt64_gbi_rdp.h"

namespace RT64 {
    namespace GBI_F3D {
        void matrix(State *state, DisplayList **dl) {
            state->rsp->matrix((*dl)->w1, (*dl)->p0(16, 8));
        }

        void popMatrix(State *state, DisplayList **dl) {
            if ((*dl)->w1 == 0) {
                state->rsp->popMatrix(1);
            }
        }
        
        void moveMem(State *state, DisplayList **dl) {
            switch ((*dl)->p0(16, 8)) {
            case F3D_G_MV_VIEWPORT:
                state->rsp->setViewport((*dl)->w1);
                break;
            case F3D_G_MV_MATRIX_1:
                state->rsp->forceMatrix((*dl)->w1);
                *dl = *dl + 3;
                break;
            case F3D_G_MV_L0:
                state->rsp->setLight(0, (*dl)->w1);
                break;
            case F3D_G_MV_L1:
                state->rsp->setLight(1, (*dl)->w1);
                break;
            case F3D_G_MV_L2:
                state->rsp->setLight(2, (*dl)->w1);
                break;
            case F3D_G_MV_L3:
                state->rsp->setLight(3, (*dl)->w1);
                break;
            case F3D_G_MV_L4:
                state->rsp->setLight(4, (*dl)->w1);
                break;
            case F3D_G_MV_L5:
                state->rsp->setLight(5, (*dl)->w1);
                break;
            case F3D_G_MV_L6:
                state->rsp->setLight(6, (*dl)->w1);
                break;
            case F3D_G_MV_L7:
                state->rsp->setLight(7, (*dl)->w1);
                break;
            case F3D_G_MV_LOOKATX:
                state->rsp->setLookAt(0, (*dl)->w1);
                break;
            case F3D_G_MV_LOOKATY:
                state->rsp->setLookAt(1, (*dl)->w1);
                break;
            default: {
                const uint32_t commandAddress = uint32_t(reinterpret_cast<uint8_t *>(*dl) - state->RDRAM);
                std::fprintf(stderr,
                    "RT64 F3D MoveMem: index=0x%02X w0=0x%08X w1=0x%08X command=0x%08X taskStart=0x%08X stack=%zu\n",
                    (*dl)->p0(16, 8), (*dl)->w0, (*dl)->w1, commandAddress,
                    state->displayListAddress, state->returnAddressStack.size());
                for (size_t stackIndex = 0; stackIndex < state->returnAddressStack.size(); stackIndex++) {
                    DisplayList *caller = state->returnAddressStack[stackIndex];
                    const uint32_t callerAddress = uint32_t(reinterpret_cast<uint8_t *>(caller) - state->RDRAM);
                    std::fprintf(stderr, "RT64 F3D MoveMem stack[%zu]: address=0x%08X w0=0x%08X w1=0x%08X\n",
                        stackIndex, callerAddress, caller->w0, caller->w1);
                }
#if defined(__ANDROID__)
                uint32_t changedBytes = 0;
                uint32_t firstChanged = 0;
                for (uint32_t offset = 0; offset < state->debugDynamicListAtStart.size(); offset++) {
                    if (state->debugDynamicListAtStart[offset] != state->RDRAM[0x1D0000u + offset]) {
                        if (changedBytes == 0) firstChanged = offset;
                        changedBytes++;
                    }
                }
                std::fprintf(stderr, "RT64 F3D dynamic list changed during parse: bytes=%u first=%08X\n",
                    changedBytes, 0x1D0000u + firstChanged);
                if (commandAddress >= 0x1D0000u && commandAddress <= 0x1DFFF8u) {
                    DisplayList original;
                    std::memcpy(&original, state->debugDynamicListAtStart.data() + commandAddress - 0x1D0000u,
                        sizeof(original));
                    std::fprintf(stderr, "RT64 F3D command at parse start: w0=%08X w1=%08X\n", original.w0, original.w1);
                }
                if (!state->returnAddressStack.empty()) {
                    const DisplayList *caller = state->returnAddressStack.back();
                    const uint32_t target = caller->w1 & 0x7FFFFFu;
                    if (caller->w0 == 0x06000000u && target <= 0x800000u - 0x1000u) {
                        uint32_t hash = 2166136261u;
                        for (uint32_t i = 0; i < 0x1000u; i++) {
                            hash = (hash ^ state->RDRAM[target + i]) * 16777619u;
                        }
                        uint32_t lastEnd = 0;
                        for (uint32_t address = target; address < commandAddress; address += 8) {
                            const DisplayList *candidate = reinterpret_cast<const DisplayList *>(state->RDRAM + address);
                            if ((candidate->w0 >> 24) == 0xB8u) {
                                lastEnd = address;
                            }
                        }
                        std::fprintf(stderr, "RT64 F3D nested target=0x%08X hashAtFault=0x%08X lastEndBeforeFault=0x%08X\n",
                            target, hash, lastEnd);
                    }
                }
                const uint32_t firstAddress = (commandAddress >= 32u * 8u) ? commandAddress - 32u * 8u : 0u;
                for (uint32_t address = firstAddress; address <= commandAddress + 8u * 8u && address <= 0x800000u - 8u; address += 8u) {
                    const DisplayList *context = reinterpret_cast<const DisplayList *>(state->RDRAM + address);
                    std::fprintf(stderr, "RT64 F3D context %08X: %08X %08X\n", address, context->w0, context->w1);
                }
#endif
                std::fflush(stderr);
                assert(false && "Unimplemented move mem.");
                break;
            }
            }
        }
        
        void vertex(State *state, DisplayList **dl) {
            state->rsp->setVertex((*dl)->w1, (*dl)->p0(20, 4) + 1, (*dl)->p0(16, 4));
        }

        void runDl(State *state, DisplayList **dl) {
            // A zero target is not a usable display list. Doraemon can recycle
            // sprite display-list memory immediately after the early SP event,
            // leaving a partially rewritten G_DL (the command word is present
            // while its address is still zero). Following it would make the
            // interpreter execute RDRAM address 0 as graphics commands.
            // Recover as if the malformed nested list had ended.
            if ((*dl)->w1 == 0) {
                *dl = state->popReturnAddress();
                return;
            }

            if ((*dl)->p0(16, 1) == 0) {
                state->pushReturnAddress(*dl);
            }

            const uint32_t rdramAddress = state->rsp->fromSegmentedMasked((*dl)->w1);
            *dl = reinterpret_cast<DisplayList *>(state->fromRDRAM(rdramAddress)) - 1;
        }

        void endDl(State *state, DisplayList **dl) {
            *dl = state->popReturnAddress();
        }

        void sprite2DBase(State *state, DisplayList **dl) {
            // TODO
        }

        void tri1(State *state, DisplayList **dl) {
            state->rsp->drawIndexedTri((*dl)->p1(16, 8) / 10, (*dl)->p1(8, 8) / 10, (*dl)->p1(0, 8) / 10);
        }
        
        void quad(State *state, DisplayList **dl) {
            const uint8_t v0 = (*dl)->p1(24, 8) / 10;
            const uint8_t v1 = (*dl)->p1(16, 8) / 10;
            const uint8_t v2 = (*dl)->p1(8, 8) / 10;
            const uint8_t v3 = (*dl)->p1(0, 8) / 10;
            state->rsp->drawIndexedTri(v0, v1, v2);
            state->rsp->drawIndexedTri(v0, v2, v3);
        }

        void cullDl(State *state, DisplayList **dl) {
            // TODO
        }

        void moveWord(State *state, DisplayList **dl) {
            uint8_t type = (*dl)->p0(0, 8);
            switch (type) {
            case G_MW_MATRIX:
                assert(false);
                // TODO
                break;
            case G_MW_NUMLIGHT:
                state->rsp->setLightCount((((*dl)->w1 - 0x80000000) >> 5) - 1);
                break;
            case G_MW_CLIP:
                state->rsp->setClipRatioEdge(((*dl)->p0(8, 16) - G_MWO_CLIP_RNX) / 8, int16_t((*dl)->w1 & 0xFFFFU));
                break;
            case G_MW_SEGMENT:
                state->rsp->setSegment((*dl)->p0(10, 4), (*dl)->w1);
                break;
            case G_MW_FOG:
                state->rsp->setFog((int16_t)((*dl)->p1(16, 16)), (int16_t)((*dl)->p1(0, 16)));
                break;
            case G_MW_LIGHTCOL:
                state->rsp->setLightColor((*dl)->p0(8, 16) / 32, (*dl)->w1);
                break;
            case F3D_G_MW_POINTS: 
                state->rsp->modifyVertex((*dl)->p0(8, 16) / 40, (*dl)->p0(8, 16) % 40, (*dl)->w1);
                break;
            case G_MW_PERSPNORM:
                // TODO
                break;
            default:
                break;
            }
        }

        void texture(State *state, DisplayList **dl) {
            uint8_t tile = (*dl)->p0(8, 3);
            uint8_t level = (*dl)->p0(11, 3);
            uint8_t on = (*dl)->p0(0, 8);
            uint16_t sc = (*dl)->p1(16, 16);
            uint16_t tc = (*dl)->p1(0, 16);
            state->rsp->setTexture(tile, level, on, sc, tc);
        }

        void setOtherModeH(State *state, DisplayList **dl) {
            state->rsp->setOtherModeH((*dl)->p0(0, 8), (*dl)->p0(8, 8), (*dl)->w1);
        }

        void setOtherModeL(State *state, DisplayList **dl) {
            state->rsp->setOtherModeL((*dl)->p0(0, 8), (*dl)->p0(8, 8), (*dl)->w1);
        }

        void setGeometryMode(State *state, DisplayList **dl) {
            state->rsp->setGeometryMode((*dl)->w1);
        }

        void clearGeometryMode(State *state, DisplayList **dl) {
            state->rsp->clearGeometryMode((*dl)->w1);
        }

        void rdpHalf1(State *state, DisplayList **dl) {
            state->microcode.half1 = (*dl)->w1;
        }

        void rdpHalf2(State *state, DisplayList **dl) {
            state->microcode.half2 = (*dl)->w1;
        }

        void setColorImage(State *state, DisplayList **dl) {
            const uint8_t fmt = (*dl)->p0(21, 3);
            const uint8_t siz = (*dl)->p0(19, 2);
            const uint16_t width = (*dl)->p0(0, 12) + 1;
            const uint32_t address = (*dl)->w1;
            state->rsp->setColorImage(fmt, siz, width, address);
        }

        void setDepthImage(State *state, DisplayList **dl) {
            const uint32_t address = (*dl)->w1;
            state->rsp->setDepthImage(address);
        }

        void setTextureImage(State *state, DisplayList **dl) {
            const uint8_t fmt = (*dl)->p0(21, 3);
            const uint8_t siz = (*dl)->p0(19, 2);
            const uint16_t width = (*dl)->p0(0, 12) + 1;
            const uint32_t address = (*dl)->w1;
            state->rsp->setTextureImage(fmt, siz, width, address);
        }

        void reset(State *state) {
            state->rsp->setLookAtVectors(hlslpp::float3(0.0f, 1.0f, 0.0f), hlslpp::float3(1.0f, 0.0f, 0.0f));
            state->rsp->setFog(0x0100, 0x0000);
        }

        void setup(GBI *gbi) {
            gbi->constants = {
                { F3DENUM::G_MTX_MODELVIEW, 0x00 },
                { F3DENUM::G_MTX_PROJECTION, 0x01 },
                { F3DENUM::G_MTX_MUL, 0x00 },
                { F3DENUM::G_MTX_LOAD, 0x02 },
                { F3DENUM::G_MTX_NOPUSH, 0x00 },
                { F3DENUM::G_MTX_PUSH, 0x04 },
                { F3DENUM::G_TEXTURE_ENABLE, 0x00000002 },
                { F3DENUM::G_SHADING_SMOOTH, 0x00000200 },
                { F3DENUM::G_CULL_FRONT, 0x00001000 },
                { F3DENUM::G_CULL_BACK, 0x00002000 },
                { F3DENUM::G_CULL_BOTH, 0x00003000 }
            };
            
            gbi->map[F3D_G_SPNOOP] = &GBI_EXTENDED::noOpHook;
            gbi->map[F3D_G_MTX] = &matrix;
            gbi->map[F3D_G_MOVEMEM] = &moveMem;
            gbi->map[F3D_G_VTX] = &vertex;
            gbi->map[F3D_G_DL] = &runDl;
            gbi->map[F3D_G_ENDDL] = &endDl;
            gbi->map[F3D_G_SPRITE2D_BASE] = &sprite2DBase;
            gbi->map[F3D_G_TRI1] = &tri1;
            gbi->map[F3D_G_QUAD] = &quad;
            gbi->map[F3D_G_CULLDL] = &cullDl;
            gbi->map[F3D_G_POPMTX] = &popMatrix;
            gbi->map[F3D_G_MOVEWORD] = &moveWord;
            gbi->map[F3D_G_TEXTURE] = &texture;
            gbi->map[F3D_G_SETOTHERMODE_H] = &setOtherModeH;
            gbi->map[F3D_G_SETOTHERMODE_L] = &setOtherModeL;
            gbi->map[F3D_G_SETGEOMETRYMODE] = &setGeometryMode;
            gbi->map[F3D_G_CLEARGEOMETRYMODE] = &clearGeometryMode;
            gbi->map[F3D_G_RDPHALF_1] = &rdpHalf1;
            gbi->map[F3D_G_RDPHALF_2] = &rdpHalf2;
            gbi->map[G_SETCIMG] = &setColorImage;
            gbi->map[G_SETZIMG] = &setDepthImage;
            gbi->map[G_SETTIMG] = &setTextureImage;
            gbi->map[G_RDPNOOP] = &GBI_RDP::noOp;

            gbi->resetFromTask = &reset;
        }
    }
};
