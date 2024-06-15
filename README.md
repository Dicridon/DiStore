# DiStore: A Full-Memory-Disaggregation-Friendly Key-Value Store with Improved Tail Latency and Space Efficiency
This is the source code of paper **DiStore: A Full-Memory-Disaggregation-Friendly Key-Value Store with Improved Tail Latency and Space Efficiency. ICPP'24**.

## Compile
The `Makefile` can be used to build the targets. `make tests` will produce all runnable binaries used by DiStore in the directory `target`. If errors occurs, please use Ruby gem `canoe` and command `canoe test store` to build DiStore. You will find it helpful to visit `canoe`'s tutorial at [canoe](https://github.com/Dicridon/canoe "canoe, cargo for C++").

A `compile_commands.json` file is already included in this repo. Any text editors (Emacs/Vim/VSCode) or IDE that uses `compile_commands.json` for LSP utility should work well with it.

## Run
There are many parameters
- `--type, -t compute/memory` specifies the role of current machine, either a compute node (CN) or memory node (MN).
- `--config, -c /path/to/config/file`. CN (or MN) should have config files to set up networks. Configure file format is explained in the next section.
- `--memory_nodes /path/to/config/file`. CN needs to know which MNs can be contacted and their information.
- `--threads, -T integer`.
- `--size, -s integer`. How many key-value pairs are populated.
- `--workload, -w A/B/C/L/R`. YCSB workloads. L and R are for load only and range only.


## Configuration
We have configuration file examples in the directory `./config_files`.
### Compute node configuration file
Format of a  compute node configuration file is as follows:
```C++
//        tcp            roce         erpc
node0: 1.1.1.1:123, 2.2.2.2:123, 3.3.3.3:123
rdma_device: mlx5_0
rdma_port: 1
gid_idx: 4
```
The three IPs are used for basic communication, exchanging RDMA handshake information and eRPC.


The compute node needs to know the information about memory nodes, which is passed by parameter `--memory_nodes`. The configuration file has the following format
```C++
node0: 127.0.0.1:1234, 123.3.4.5:4567, 543.2.1.3:8966
node1: 431.1.3.45:5432, 432.12.3.5:4832, 564.6.7.8:123578
```

### Memory node configuration files
The format of memory node configuration file is similar to compute node configuration file, with one extra field `mem_cap` to sepcify memory capacity of this node.
```C++
//       tcp              roce            erpc
node1: 127.0.0.1:1234, 127.0.0.1:4321, 127.0.0.1:3124
mem_cap: 1024
rdma_device: mlx5_0
rdma_port: 1
gid_idx: 2
```


## Dependencies
We need runnable eRPC for RPC functionality. However, the eRPC hard-codes RDMA GID in the code. Please change the value according to the machine configurations

# Hints
- Currently, due to the limitation eRPC, we preallocate all needed memory at startup time. This eliminates runtime memory segment allocation overhead. Since the overhead is negligible, the pre-allocation does not significantly affect the evaluation results.

- If the preallocated memory is used up and the working threads need to require more remote memory, ePRC can fail due to mismatching thread-local contexts. To run larger workloads, please use large segment size in `src/components/memory/memory.hpp`

- Performance sampling is enabled by default, thus the aggregated throughput may not match the results in the paper. For higher throughput, use `nullptr` to replace the `Stats::Breakdown breakdown` and `Stats::Operation operation` variable in `test_store.cpp`.

- If you have any trouble building & running the code, feel free to contact at xiongziwei@ict.ac.cn.

## TODO
- DiStore currently do not have a automatic performance stats collector. Users has to collect all the stats by hand.
- I haven't test the source code on different operating systems and hardware settings. I can only guarantee that the source code can run on Ubuntu using `canoe`.
