//
// RT64
//

#include "rt64_interpreter.h"

#include <cassert>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

//#define DUMP_DISPLAY_LISTS

namespace RT64 {
    static FILE *displayListFp = nullptr;

    // Interpreter

    Interpreter::Interpreter() {
        state = nullptr;
        hleGBI = nullptr;
        extendedFunction = gbiManager.getExtendedFunction();
    }

    void Interpreter::setup(State *state) {
        this->state = state;
    }

    void Interpreter::loadUCodeGBI(uint32_t textAddress, uint32_t dataAddress, bool resetFromTask) {
        if (!resetFromTask) {
            state->flush();
        }

        const uint32_t AddressMask = 0xFFFFF8;
        const uint32_t maskedTextAddress = textAddress & AddressMask;
        const uint32_t maskedDataAddress = dataAddress & AddressMask;
        if ((UCode.textAddress != maskedTextAddress) || (UCode.dataAddress != maskedDataAddress)) {
            hleGBI = gbiManager.getGBIForUCode(state->RDRAM, maskedTextAddress, maskedDataAddress);
            if (hleGBI != nullptr) {
                state->rsp->setGBI(hleGBI);
            }

            UCode.textAddress = maskedTextAddress;
            UCode.dataAddress = maskedDataAddress;
        }

        if (hleGBI != nullptr) {
            GBIReset resetFunction = resetFromTask ? hleGBI->resetFromTask : hleGBI->resetFromLoad;
            if (resetFunction != nullptr) {
                resetFunction(state);
            }
        }
    }

    void Interpreter::processRDPLists(uint32_t dlStartAdddress, DisplayList *dlStart, DisplayList *dlEnd) {
        state->dlCpuProfiler.start();

        // Update the state with the current display list address.
        state->displayListAddress = dlStartAdddress;
        state->displayListCounter++;

        // Check RDRAM if required.
        state->checkRDRAM();

        GBI *rdpGBI = state->rdp->gbi;
        constexpr unsigned int opCodeMask = 0x3F;

        // Run the command interpreter.
        assert(rdpGBI != nullptr);
        DisplayList *dl = dlStart;
        uint8_t opCode;
        GBIFunction func;
        uint32_t cmdLength;
        size_t pendingCommandRemainingBytes = state->rdp->pendingCommandRemainingBytes;

        if (dlStart >= dlEnd) {
            state->dlCpuProfiler.end();
            return;
        }

        if (pendingCommandRemainingBytes != 0) {
            // Copy the remaining command bytes from the current displaylist
            uint32_t toCopy = (uint32_t)std::min(pendingCommandRemainingBytes, (uintptr_t)dlEnd - (uintptr_t)dl);
            memcpy(state->rdp->pendingCommandBuffer.data() + state->rdp->pendingCommandCurrentBytes, dl, toCopy);

            // Modify start to skip the copied bytes
            dl = (DisplayList *)(toCopy + (uintptr_t)dl);

            // Check if we've copied all of the bytes of the command into the buffer
            if (pendingCommandRemainingBytes == toCopy) {
                // All bytes have been copied, so run the completed command
                DisplayList *pendingCommand = (DisplayList *)state->rdp->pendingCommandBuffer.data();
                opCode = (pendingCommand->w0 >> 24) & opCodeMask;
                func = rdpGBI->map[opCode];

                if (func != nullptr) {
                    func(state, &pendingCommand);
                }
                else {
                    RT64_LOG_PRINTF("DL Parser ran into an unknown RDP opCode: %u / 0x%X", opCode, opCode);
                }

                state->rdp->pendingCommandCurrentBytes = 0;
                state->rdp->pendingCommandRemainingBytes = 0;
            }
            // Not all of the bytes were copied, so adjust RDP state accordingly and exit.
            else {
                state->rdp->pendingCommandCurrentBytes += toCopy;
                state->rdp->pendingCommandRemainingBytes -= toCopy;
                state->dlCpuProfiler.end();
                return;
            }
        }

        // Create a dummy pointer and pass that, since displaylist pointer incrementing is handled differently in LLE.
        DisplayList *dummy;
        while ((dl != nullptr) && ((dlEnd == nullptr) || (dl < dlEnd))) {
            opCode = (dl->w0 >> 24) & opCodeMask;

            if ((extendedOpCode != 0) && (opCode == extendedOpCode)) {
                dummy = dl;
                extendedFunction(state, &dl);
                cmdLength = 1;
            }
            else {
                func = rdpGBI->map[opCode];
                cmdLength = state->rdp->commandWordLengths[opCode];

#       ifdef DUMP_DISPLAY_LISTS
                RT64_LOG_PRINTF("0x%08X 0x%08X", dl->w0, dl->w1);
#       endif

                // Check if this command is unfinished and store the partial contents if so.
                if (dl + cmdLength > dlEnd) {
                    uint32_t toCopy = (uint32_t)((uintptr_t)dlEnd - (uintptr_t)dl);
                    memcpy(state->rdp->pendingCommandBuffer.data(), dl, toCopy);
                    state->rdp->pendingCommandCurrentBytes = toCopy;
                    state->rdp->pendingCommandRemainingBytes = cmdLength * sizeof(DisplayList) - toCopy;
                    break;
                }

                if (func != nullptr) {
                    dummy = dl;
                    func(state, &dummy);
                }
                else {
                    RT64_LOG_PRINTF("DL Parser ran into an unknown RDP opCode: %u / 0x%X", opCode, opCode);
                }
            }

            if (dl != nullptr) {
                dl += cmdLength;
            }
        }

        state->dlCpuProfiler.end();
    }

    void Interpreter::processDisplayLists(uint32_t dlStartAdddress, DisplayList *dlStart) {
        assert(hleGBI != nullptr);

        state->dlCpuProfiler.start();

        // Update the state with the current display list address.
        state->displayListAddress = dlStartAdddress;
        state->displayListCounter++;

        // Check RDRAM if required.
        state->checkRDRAM();

        // Run the command interpreter.
        DisplayList *dl = dlStart;
        uint8_t opCode;
        GBIFunction func;
#if defined(__ANDROID__)
        // Snapshot Doraemon's dynamic nested-list region before parsing.
        std::memcpy(state->debugDynamicListAtStart.data(), state->RDRAM + 0x1D0000u,
            state->debugDynamicListAtStart.size());
        bool firstUnknownLogged = false;
#endif
        while (dl != nullptr) {
            opCode = (dl->w0 >> 24);

            if ((extendedOpCode != 0) && (opCode == extendedOpCode)) {
                extendedFunction(state, &dl);
            }
            else {
                func = hleGBI->map[opCode];

#       ifdef DUMP_DISPLAY_LISTS
                RT64_LOG_PRINTF("0x%08X 0x%08X", dl->w0, dl->w1);
#       endif

                if (func != nullptr) {
                    func(state, &dl);
                }
                else {
#if defined(__ANDROID__)
                    if (!firstUnknownLogged) {
                        firstUnknownLogged = true;
                        const uint32_t commandAddress = uint32_t(reinterpret_cast<uint8_t *>(dl) - state->RDRAM);
                        uint32_t changedBytes = 0;
                        uint32_t firstChanged = 0;
                        for (uint32_t offset = 0; offset < state->debugDynamicListAtStart.size(); offset++) {
                            if (state->debugDynamicListAtStart[offset] != state->RDRAM[0x1D0000u + offset]) {
                                if (changedBytes == 0) firstChanged = offset;
                                changedBytes++;
                            }
                        }
                        std::fprintf(stderr,
                            "RT64 first unknown DL: task=%08X command=%08X w0=%08X w1=%08X gbi=%u stack=%zu dynamicChanged=%u firstChanged=%08X\n",
                            dlStartAdddress, commandAddress, dl->w0, dl->w1,
                            uint32_t(hleGBI->ucode), state->returnAddressStack.size(),
                            changedBytes, 0x1D0000u + firstChanged);
                        if (commandAddress >= 0x1D0000u && commandAddress <= 0x1DFFF8u) {
                            const uint32_t offset = commandAddress - 0x1D0000u;
                            DisplayList original;
                            std::memcpy(&original, state->debugDynamicListAtStart.data() + offset, sizeof(original));
                            std::fprintf(stderr, "RT64 first unknown at parse start: w0=%08X w1=%08X\n", original.w0, original.w1);
                        }
                        for (size_t index = 0; index < state->returnAddressStack.size(); index++) {
                            const DisplayList *caller = state->returnAddressStack[index];
                            std::fprintf(stderr, "RT64 unknown stack[%zu]: address=%08X w0=%08X w1=%08X\n",
                                index, uint32_t(reinterpret_cast<const uint8_t *>(caller) - state->RDRAM), caller->w0, caller->w1);
                        }
                        if (commandAddress <= 0x800000u - 8u) {
                            const uint32_t firstAddress = commandAddress >= 16u * 8u ? commandAddress - 16u * 8u : 0u;
                            const uint32_t lastAddress = std::min(commandAddress + 4u * 8u, 0x800000u - 8u);
                            for (uint32_t address = firstAddress; address <= lastAddress; address += 8u) {
                                const DisplayList *context = reinterpret_cast<const DisplayList *>(state->RDRAM + address);
                                std::fprintf(stderr, "RT64 unknown context %08X: %08X %08X\n", address, context->w0, context->w1);
                            }
                        }
                        std::fflush(stderr);
                    }
#endif
                    RT64_LOG_PRINTF("DL Parser ran into an unknown opCode (GBI %u): %u / 0x%X", uint32_t(hleGBI->ucode), opCode, opCode);
                }
            }

            if (dl != nullptr) {
                dl++;
            }
        }

        state->dlCpuProfiler.end();
    }
};
