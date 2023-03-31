################################################################################
#
# Copyright (C) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
################################################################################

from ..Component import SumUnroll
from ..Common import printExit
from ..TensileInstructions import Module, VDot2F32F16, SMovB32, vgpr, sgpr
from math import ceil

class SumUnrollMfma(SumUnroll):
    kernel = {"EnableMatrixInstruction": True}

    """
    Sum unroll for reduction
    Use the same pattern as mfma
    """
    def __call__(self, writer, kernel, tc, u, innerUnroll):
        imod = Module("SumUnroll%s_I%s" % (tc, innerUnroll))

        m = (u) % (writer.states.numVgprBuffer+1) # local to use for MACs

        # calculate constant
        numRegistersIn   = kernel["ProblemType"]["DataType"].numRegisters()
        numMIInput       = kernel["MIInputPerThread"]
        vgprPerInput     = int(numMIInput * numRegistersIn)

        waveTile = kernel["MIWaveTile"][0] if tc == "A" else kernel["MIWaveTile"][1]
        numIterPerCoalescedRead = writer.states.numIterPerCoalescedReadA if tc == "A" else writer.states.numIterPerCoalescedReadB
        # here we remap index to where it read for wider local read
        # ex. if we read 2 iteration at a time,
        #   original   : _ds_load_b64  valuA_X0_I0
        #   read 2 iter: _ds_load_b128 valuA_X0_I0 (we read valuA_X0_I0 and valuA_X1_I0)
        # instead of using valuA_X1_I0, we use valuA_X0_I0+2 as mfma input

        vgprBuffer_new = (m//numIterPerCoalescedRead)*numIterPerCoalescedRead
        vgprBuffer_new_offset = m%numIterPerCoalescedRead*kernel["InnerUnroll"]*vgprPerInput

        tmpSgpr = -1
        if numRegistersIn < 1:
            assert kernel["ProblemType"]["DataType"].isHalf()
            tmpSgpr = writer.sgprPool.checkOut(1)
            imod.add(SMovB32(dst=sgpr(tmpSgpr), src=hex(0x3c003c00), comment="packed 1.0"))

        for iui in range(0, innerUnroll):
            iui_new = (iui//numIterPerCoalescedRead)*numIterPerCoalescedRead
            iui_new_offset = iui%numIterPerCoalescedRead*vgprPerInput
            for idx in range(0, waveTile):
                new     = idx*vgprPerInput*numIterPerCoalescedRead
                # valuStr = "Valu%s_X%u_I%u+%u+%u+%u" % (tc, vgprBuffer_new, iui_new, new, vgprBuffer_new_offset, iui_new_offset)
                valuStr    = "Valu%s_X%u_I%u+%u+%u" % (tc, vgprBuffer_new, iui_new, new, vgprBuffer_new_offset)
                valuSumStr = "ValuSum+%u"%idx
                if kernel["ProblemType"]["DataType"].isHalf():
                    # First version only supports mfma with K > 1
                    if vgprPerInput > 1 and (vgprPerInput % 2 == 0):
                        for inputIdx in range(0, vgprPerInput, 2):
                            imod.add(VDot2F32F16(dst=vgpr(valuSumStr), src0=vgpr("%s+%s"%(valuStr, iui_new_offset + inputIdx)), src1=sgpr(tmpSgpr), src2=vgpr(valuSumStr), comment="sum K"))
                    else:
                        printExit("Currently unsupported vgprPerInput %u"%vgprPerInput)
                else:
                    printExit("Currently unsupported data type")
                print("sum k: ",kernel["MatrixInstK"], "iui=", valuStr, valuStr, iui, idx)

        if tmpSgpr != -1:
            writer.sgprPool.checkIn(tmpSgpr)

        return imod
