# Symbol sharding (v1.1)

Map `symbol_id → OrderBook*` with one thread per hot symbol (or a pool of
workers each owning a disjoint symbol set). Never migrate a live book across
cores without quiescing.
