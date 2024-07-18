# rdma-prot

## Workflow

```mermaid
graph TD
    A[Start] --> B[Initialize RDMA resources]
    B --> C[Establish TCP connection]
    C --> D[Exchange RDMA info]
    D --> E[Connect QPs]
    E --> F{Compute Node?}
    F -->|Yes| G[Prepare Xlogs]
    F -->|No| H[Initialize receive buffer]
    G --> I[RDMA Write Xlog]
    H --> J[Check for new data]
    I --> K{More Xlogs?}
    J --> L{New data?}
    K -->|Yes| G
    K -->|No| M[Clean up]
    L -->|Yes| N[Process Xlog]
    L -->|No| J
    N --> O{All Xlogs received?}
    O -->|No| J
    O -->|Yes| M
    M --> P[End]