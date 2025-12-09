# Variable Order Markov Model (VOMM) Algorithm Documentation

## Overview

The Variable Order Markov Model (VOMM) implementation in `preload-ng` is a specialized hybrid algorithm designed for process prediction. It combines the strengths of **Prediction by Partial Matching (PPM)** for sequence learning with a **Dependency Graph (DG)** approach for immediate transition probabilities.

The goal of this algorithm is to predict the next set of applications the user is likely to launch based on their current and recent usage history.

## Core Concepts

The algorithm operates on two primary data structures maintained in the global `vomm_system`:

1.  **Prediction Tree**: A trie-like structure where nodes represent execution contexts.

    - **Root Node**: Represents the empty context (start of a session or no history).
    - **Child Nodes**: Represent executable launches that follow the parent context.
    - **Edges**: Represent transitions between states, weighted by frequency (counts).

2.  **Global History**: A sliding window list of the most recent execution events (limited to `MAX_VOMM_DEPTH = 5`). This serves as the short-term memory for establishing the current context.

## Data Structures

### VOMM Node (`vomm_node_t`)

Each node in the tree contains:

- `exe`: Pointer to the executable associated with this node.
- `children`: A Hash Table mapping next executable paths to child nodes.
- `count`: Frequency counter for how often this specific sequence path has occurred.
- `parent`: Back-pointer to the parent node.

## 1. Update Phase (Training)

The model is trained online, updating continuously as applications are launched. The `vomm_update(preload_exe_t *exe)` function handles this process in three steps:

### A. History Maintenance

The system appends the new executable to the global history list. To maintain performance and relevance, the history acts as a sliding window with a maximum depth of 5. Older entries are pruned when the limit is reached.

### B. Tree Structure Update (Sequence Learning)

The algorithm extends the tree from the `current_context` pointer.

1.  It checks if the new executable exists as a child of the current context node.
2.  If not, a new child node is created.
3.  The transition count is incremented.
4.  The `current_context` pointer is advanced to this new node.

This builds effectively a Variable Order Markov Model where paths from the root represent observed sequences of events.

### C. Global Bigram Update (Dependency Graph)

To ensure that simple pairwise relationships (A -> B) are always captured regardless of the deeper context, the algorithm explicitly updates specific paths from the root:

- It looks at the _immediate previous_ executable in the history.
- It updates (or creates) the transition `Root -> [Previous Exe] -> [Current Exe]`.

This acts as a "Generalist" layer, reinforcing the basic probability of Exe B following Exe A.

## 2. Prediction Phase

The prediction logic (`vomm_predict`) uses a multi-layered voting strategy to generate candidates for preloading. It calculates a "bid" (probability score) for various executables.

The strategy assumes a "Hybrid" approach consisting of two main layers:

### Layer 1: Context-Specific Prediction (PPM & DG)

This layer looks at the specific execution history to find patterns.

**A. History Context Iteration**
The algorithm iterates through _every_ item in the recent history list. For each item:

1.  It treats that item as a context (transitioning from `Root -> [History Item]`).
2.  It examines all children of that context.
3.  **PPM Logic**: It calculates a confidence score based on the child's frequency relative to the total transitions from that context.
    - _Confidence = Child Count / Total Context Count_
    - Executables already running are skipped.
    - The probability is added to the candidate's score logarithmically.

**B. Deep Context Analysis (Order-K)**
If the `current_context` is deep in the tree (not root), it implies a specific sequence (e.g., A -> B -> C). The algorithm performs:

1.  **PPM Prediction**: Same as above, but strictly for the current deep context.
2.  **DG Fallback (Dependency Graph)**: It applies a generalized "weak bid" (constant probability boost) to all immediate neighbors of the current context. This ensures that even low-frequency transitions in the current specific sequence get a slight boost.

### Layer 2: Global Frequency Fallback

If the context-specific layers fail to find strong candidates, or to supplement them, the algorithm applies a global frequency model.

1.  It calculates the total number of transitions observed across the _entire_ tree.
2.  It iterates through every transition in the tree.
3.  It calculates a global confidence score:
    - _Global Confidence = (Node Count / Total Global Count)_
4.  **Dampening**: To prevent global popularity from overshadowing context-specific predictions, the score is scaled to a maximum of 0.5 (50% probability).
    - _Scaled Score = 0.1 + (Global Confidence _ 0.4)\*

## Mathematical Model

The predictions are accumulated using Log-Probabilities (`lnprob`) in the executable structures.

- High confidence predictions (near 1.0) contribute significantly to the score (adding `log(1 - confidence)`).
- The system includes safeguards to avoid `log(0)` for 100% confidence matches (capped at 0.99).

## Summary

This VOMM implementation balances specificity and generality:

- **Specificity**: By tracking sequences (A->B->C), it learns complex workflows.
- **Generality**: By explicitly updating bigrams (A->B) and using Global Frequency, it handles interruptions and noise effectively.
- **Efficiency**: The tree structure is sparse (only storing observed sequences) and the history window is small, keeping memory usage low and lookups fast ($O(1)$ for hash map interactions).
