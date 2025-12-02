# Fast TCP
This codebase has two fast tcp versions implemented. The files `src/internet/model/tcp-fast.cc` takes inspiration from the ns-2 implementation of tcp fast of original authors (found after much struggle), whereas `src/internet/model/tcp-fast-1.cc` is our own implementation from the understanding of the paper. Both implementations provided similar results by tuning the parameters `alpha`. The ns-2 implementation is more similar to tcp vegas by defining 2 parameters `alpha` and `beta`.

## Build steps
1. Clone this repo using `git clone <repo name>` command.
2. Install clang, cmake, ccache. (prefer unix based system for easier setup)
3. Use this command to build ns-3 with examples
`$ ./ns3 configure --enable-examples --enable-tests`
`$ ./ns3 build`
4. The examples are written in `examples/tcp/` folder where simulation 1 is `reno-equilibrium.cc`, simulation 2 is `tcp-multi-rtt-bottleneck.cc`, simulation 3 is `og-sim-2.cc`.
5. To run any of them - 
`$ ./ns3 run 'reno-equilibrium'`
Running the above commands will generate files of traces of simulation.
6. To generate and run plots, the python scripts in `results/<simulation-name>` can be used.
7. It is recommended to generate a virtual env, install matplotlib, and simulate using the python scripts.
```
$ python3 -m venv env     # env is the name of the virtual environment
$ source env/bin/activate
$ pip install matplotlib
$ python3 results/<simulation-name>/run_and_plot.py
```

