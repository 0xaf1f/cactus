/*
 * Copyright (C) 2009-2011 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include "endAligner.h"
#include "multipleAligner.h"
#include "adjacencySequences.h"
#include "pairwiseAligner.h"

AlignedPair *alignedPair_construct(int64_t subsequenceIdentifier1, int64_t position1, bool strand1,
        int64_t subsequenceIdentifier2, int64_t position2, bool strand2, int64_t score, int64_t rScore) {
    AlignedPair *alignedPair = st_malloc(sizeof(AlignedPair));
    alignedPair->subsequenceIdentifier = subsequenceIdentifier1;
    alignedPair->position = position1;
    alignedPair->strand = strand1;

    alignedPair->reverse = st_malloc(sizeof(AlignedPair));
    alignedPair->reverse->reverse = alignedPair;

    alignedPair->reverse->subsequenceIdentifier = subsequenceIdentifier2;
    alignedPair->reverse->position = position2;
    alignedPair->reverse->strand = strand2;

    alignedPair->score = score;
    alignedPair->reverse->score = rScore;

    return alignedPair;
}

void alignedPair_destruct(AlignedPair *alignedPair) {
    free(alignedPair); //We assume the reverse will be free independently.
}

static int alignedPair_cmpFnP(const AlignedPair *alignedPair1, const AlignedPair *alignedPair2) {
    int i = cactusMisc_nameCompare(alignedPair1->subsequenceIdentifier, alignedPair2->subsequenceIdentifier);
    if(i == 0) {
        i = alignedPair1->position > alignedPair2->position ? 1 : (alignedPair1->position < alignedPair2->position ? -1 : 0);
        if(i == 0) {
            i = alignedPair1->strand == alignedPair2->strand ? 0 : (alignedPair1->strand ? 1 : -1);
        }
    }
    return i;
}

int alignedPair_cmpFn(const AlignedPair *alignedPair1, const AlignedPair *alignedPair2) {
    int i = alignedPair_cmpFnP(alignedPair1, alignedPair2);
    if(i == 0) {
        i = alignedPair_cmpFnP(alignedPair1->reverse, alignedPair2->reverse);
    }
    return i;
}

stSortedSet *makeEndAlignment(End *end, int64_t spanningTrees, int64_t maxSequenceLength,
        int64_t maximumNumberOfSequencesBeforeSwitchingToFast, float gapGamma,
        PairwiseAlignmentParameters *pairwiseAlignmentBandingParameters) {
    //Make an alignment of the sequences in the ends

    //Get the adjacency sequences to be aligned.
    Cap *cap;
    End_InstanceIterator *it = end_getInstanceIterator(end);
    stList *sequences = stList_construct3(0, (void (*)(void *))adjacencySequence_destruct);
    stList *seqFrags = stList_construct3(0, (void (*)(void *))seqFrag_destruct);
    while((cap = end_getNext(it)) != NULL) {
        if(cap_getSide(cap)) {
            cap = cap_getReverse(cap);
        }
        AdjacencySequence *adjacencySequence = adjacencySequence_construct(cap, maxSequenceLength);
        stList_append(sequences, adjacencySequence);
        assert(cap_getAdjacency(cap) != NULL);
        stList_append(seqFrags, seqFrag_construct(adjacencySequence->string, 0, end_getName(cap_getEnd(cap_getAdjacency(cap)))));
    }
    end_destructInstanceIterator(it);

    //Get the alignment.
    MultipleAlignment *mA = makeAlignment(seqFrags, spanningTrees, 100000000, maximumNumberOfSequencesBeforeSwitchingToFast, gapGamma, pairwiseAlignmentBandingParameters);
    //Build an array of weights to reweight pairs in the alignment.
    stSortedSet *sortedAlignment =
            stSortedSet_construct3((int (*)(const void *, const void *))alignedPair_cmpFn,
            (void (*)(void *))alignedPair_destruct);
    int64_t *pairwiseAlignmentsPerGenome = getPairwiseAlignmentsPerGenome(seqFrags, mA->chosenPairwiseAlignments);
    double *scoreAdjustments = st_malloc(stList_length(seqFrags) * sizeof(double));
    if(stList_length(seqFrags) > 1) {
        for(int64_t i=0; i<stList_length(seqFrags); i++) {
            assert(pairwiseAlignmentsPerGenome[i] > 0);
            assert(pairwiseAlignmentsPerGenome[i] < stList_length(seqFrags));
            scoreAdjustments[i] = ((double)stList_length(seqFrags)-1) / pairwiseAlignmentsPerGenome[i];
            assert(scoreAdjustments[i] >= 1.0);
            assert(scoreAdjustments[i] <= stList_length(seqFrags)-1 + 0.0001);
        }
    }
	//Convert the alignment pairs to an alignment of the caps..
    while(stList_length(mA->alignedPairs) > 0) {
        stIntTuple *alignedPair = stList_pop(mA->alignedPairs);
        assert(stIntTuple_length(alignedPair) == 5);
        int64_t seqIndex1 = stIntTuple_get(alignedPair, 1);
        int64_t seqIndex2 = stIntTuple_get(alignedPair, 3);
        AdjacencySequence *i = stList_get(sequences, seqIndex1);
        AdjacencySequence *j = stList_get(sequences, seqIndex2);
        assert(i != j);
        int64_t offset1 = stIntTuple_get(alignedPair, 2);
        int64_t offset2 = stIntTuple_get(alignedPair, 4);
        int64_t score = stIntTuple_get(alignedPair, 0);
        assert(score > 0 && score <= PAIR_ALIGNMENT_PROB_1);
        assert(pairwiseAlignmentsPerGenome[seqIndex1] > 0);
        assert(pairwiseAlignmentsPerGenome[seqIndex2] > 0);
        AlignedPair *alignedPair2 = alignedPair_construct(
                i->subsequenceIdentifier, i->start + (i->strand ? offset1 : -offset1), i->strand,
                j->subsequenceIdentifier, j->start + (j->strand ? offset2 : -offset2), j->strand,
                score*scoreAdjustments[seqIndex1], score*scoreAdjustments[seqIndex2]); //Do the reweighting here.
        assert(stSortedSet_search(sortedAlignment, alignedPair2) == NULL);
        assert(stSortedSet_search(sortedAlignment, alignedPair2->reverse) == NULL);
        stSortedSet_insert(sortedAlignment, alignedPair2);
        stSortedSet_insert(sortedAlignment, alignedPair2->reverse);
        stIntTuple_destruct(alignedPair);
    }

    //Cleanup
    stList_destruct(seqFrags);
    stList_destruct(sequences);
    free(pairwiseAlignmentsPerGenome);
    free(scoreAdjustments);
    multipleAlignment_destruct(mA);

    return sortedAlignment;
}

void writeEndAlignmentToDisk(End *end, stSortedSet *endAlignment, FILE *fileHandle) {
    fprintf(fileHandle, "%s %" PRIi64 "\n", cactusMisc_nameToStringStatic(end_getName(end)), stSortedSet_size(endAlignment));
    stSortedSetIterator *it = stSortedSet_getIterator(endAlignment);
    AlignedPair *aP;
    while((aP = stSortedSet_getNext(it)) != NULL) {
        fprintf(fileHandle, "%" PRIi64 " %" PRIi64 " %i %" PRIi64 " ", aP->subsequenceIdentifier, aP->position, aP->strand, aP->score);
        aP = aP->reverse;
        fprintf(fileHandle, "%" PRIi64 " %" PRIi64 " %i %" PRIi64 "\n", aP->subsequenceIdentifier, aP->position, aP->strand, aP->score);
    }
    stSortedSet_destructIterator(it);
}

stSortedSet *loadEndAlignmentFromDisk(Flower *flower, FILE *fileHandle, End **end) {
    stSortedSet *endAlignment =
                stSortedSet_construct3((int (*)(const void *, const void *))alignedPair_cmpFn,
                (void (*)(void *))alignedPair_destruct);
    char *line = stFile_getLineFromFile(fileHandle);
    if(line == NULL) {
        *end = NULL;
        return NULL;
    }
    Name flowerName;
    int64_t lineNumber;
    int64_t i = sscanf(line, "%" PRIi64 " %" PRIi64 "", &flowerName, &lineNumber);
    if(i != 2 || lineNumber < 0) {
        st_errAbort("We encountered a mis-specified name in loading the first line of an end alignment from the disk: '%s'\n", line);
    }
    free(line);
    *end = flower_getEnd(flower, flowerName);
    if(*end == NULL) {
        st_errAbort("We encountered an end name that is not in the database: '%s'\n", line);
    }
    for(int64_t i=0; i<lineNumber; i++) {
        line = stFile_getLineFromFile(fileHandle);
        if(line == NULL) {
            st_errAbort("Got a null line when parsing an end alignment\n");
        }
        int64_t sI1, sI2;
        int64_t p1, st1, p2, st2, score1, score2;
        int64_t i = sscanf(line, "%" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 " %" PRIi64 "", &sI1, &p1, &st1, &score1, &sI2, &p2, &st2, &score2);
        (void)i;
        if(i != 8) {
            st_errAbort("We encountered a mis-specified name in loading an end alignment from the disk: '%s'\n", line);
        }
        stSortedSet_insert(endAlignment, alignedPair_construct(sI1, p1, st1, sI2, p2, st2, score1, score2));
        free(line);
    }
    return endAlignment;
}

