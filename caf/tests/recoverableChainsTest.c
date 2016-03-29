#include "CuTest.h"
#include "sonLib.h"
#include "stCaf.h"
#include "stPinchGraphs.h"

// Adds a thread with random nucleotides to the flower, and return its corresponding name in the pinch graph.
static Name addThreadToFlower(Flower *flower, int64_t length) {
    char *dna = stRandom_getRandomDNAString(length, true, true, true);
    MetaSequence *metaSequence = metaSequence_construct(2, length, dna, "", 0, flower_getCactusDisk(flower));
    Sequence *sequence = sequence_construct(metaSequence, flower);

    End *end1 = end_construct2(0, 0, flower);
    End *end2 = end_construct2(1, 0, flower);
    Cap *cap1 = cap_construct2(end1, 1, 1, sequence);
    Cap *cap2 = cap_construct2(end2, length + 2, 1, sequence);
    cap_makeAdjacent(cap1, cap2);

    free(dna);
    return cap_getName(cap1);
}

// Test a simple case where two threads only align in a single
// chain--this should obviously not be recoverable.
static void testDoesNotRemoveIsolatedChain(CuTest *testCase) {
    CactusDisk *cactusDisk = testCommon_getTemporaryCactusDisk();
    Flower *flower = flower_construct2(0, cactusDisk);
    group_construct2(flower);
    flower_check(flower);

    Name thread1Name = addThreadToFlower(flower, 100);
    Name thread2Name = addThreadToFlower(flower, 100);
    stPinchThreadSet *threadSet = stCaf_setup(flower);
    stPinchThread *thread1 = stPinchThreadSet_getThread(threadSet, thread1Name);
    stPinchThread *thread2 = stPinchThreadSet_getThread(threadSet, thread2Name);
    // Three 10-bp blocks separated by 20 bp
    stPinchThread_pinch(thread1, thread2, 10, 10, 10, true);
    stPinchThread_pinch(thread1, thread2, 40, 40, 10, true);
    stPinchThread_pinch(thread1, thread2, 70, 70, 10, true);
    // There should now be 7 blocks -- one for each cap, and the blocks we just added
    CuAssertIntEquals(testCase, 7, stPinchThreadSet_getTotalBlockNumber(threadSet));
    // Run the remove-recoverable-chains code
    stCaf_meltRecoverableChains(flower, threadSet, true, 1000, NULL);
    // It shouldn't've removed any blocks
    CuAssertIntEquals(testCase, 7, stPinchThreadSet_getTotalBlockNumber(threadSet));

    stPinchThreadSet_destruct(threadSet);
    testCommon_deleteTemporaryCactusDisk(cactusDisk);
}

CuSuite *recoverableChainsTestSuite(void) {
    CuSuite *suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, testDoesNotRemoveIsolatedChain);
    return suite;
}
