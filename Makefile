CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -Ishared
LDFLAGS = -lm

BUILD   = build
SHARED  = shared

SHARED_SRCS = parser.c objective.c output.c runner.c mmr.c local_search.c string_ga.c
SHARED_OBJS = $(addprefix $(BUILD)/,$(SHARED_SRCS:.c=.o))
FAST_OBJ    = $(BUILD)/local_search_fast.o
PHASED_OBJ    = $(BUILD)/local_search_phased.o
CIRCULAR_OBJ   = $(BUILD)/local_search_circular.o
THRESHOLD_OBJ     = $(BUILD)/local_search_threshold.o
INSTRUMENTED_OBJ  = $(BUILD)/local_search_instrumented.o
TOP50_OBJ         = $(BUILD)/local_search_top50.o

ALGOS = 1_fixed_order_sequential_greedy \
        2_global_selection_assignment_greedy \
        3_greedy_local_search \
        3_1_greedy_local_search \
        4_mmr_phased_local_search \
        4_1_circular_local_search \
        4_2_circular_threshold_local_search \
        4_3_instrumented_circular_local_search \
        4_4_Final_top50 \
        5_mmr_forbidden_rebuild \
        6_4_1_seeded_string_ga \
        9_circular_4_cycle \
        parameter_control_heuristic

BINS = $(addprefix $(BUILD)/,$(ALGOS))

all: $(BINS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SHARED)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(FAST_OBJ): $(SHARED)/local_search_fast.c $(SHARED)/local_search_fast.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_fast.c -o $@

$(PHASED_OBJ): $(SHARED)/local_search_phased.c $(SHARED)/local_search_phased.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_phased.c -o $@

$(CIRCULAR_OBJ): $(SHARED)/local_search_circular.c $(SHARED)/local_search_circular.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_circular.c -o $@

$(THRESHOLD_OBJ): $(SHARED)/local_search_threshold.c $(SHARED)/local_search_threshold.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_threshold.c -o $@

$(INSTRUMENTED_OBJ): $(SHARED)/local_search_instrumented.c $(SHARED)/local_search_instrumented.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_instrumented.c -o $@

$(TOP50_OBJ): $(SHARED)/local_search_top50.c $(SHARED)/local_search_top50.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SHARED)/local_search_top50.c -o $@

define ALGO_RULE
$(BUILD)/$(1): algorithms/$(1)/main.c $(SHARED_OBJS) $(if $(filter 3_1_%,$(1)),$(FAST_OBJ),) $(if $(filter 4_mmr_phased%,$(1)),$(PHASED_OBJ),) $(if $(filter 4_1_%,$(1)),$(CIRCULAR_OBJ),) $(if $(filter 4_2_%,$(1)),$(THRESHOLD_OBJ),) $(if $(filter 4_3_%,$(1)),$(INSTRUMENTED_OBJ),) $(if $(filter 4_4_% 6_4_1_%,$(1)),$(TOP50_OBJ),) | $(BUILD)
	$(CC) $(CFLAGS) -o $$@ algorithms/$(1)/main.c $(SHARED_OBJS) $(if $(filter 3_1_%,$(1)),$(FAST_OBJ),) $(if $(filter 4_mmr_phased%,$(1)),$(PHASED_OBJ),) $(if $(filter 4_1_%,$(1)),$(CIRCULAR_OBJ),) $(if $(filter 4_2_%,$(1)),$(THRESHOLD_OBJ),) $(if $(filter 4_3_%,$(1)),$(INSTRUMENTED_OBJ),) $(if $(filter 4_4_% 6_4_1_%,$(1)),$(TOP50_OBJ),) $(LDFLAGS)
endef

$(foreach algo,$(ALGOS),$(eval $(call ALGO_RULE,$(algo))))

clean:
	rm -rf $(BUILD)

run-1: $(BUILD)/1_fixed_order_sequential_greedy
	./$(BUILD)/1_fixed_order_sequential_greedy --save-csv

run-2: $(BUILD)/2_global_selection_assignment_greedy
	./$(BUILD)/2_global_selection_assignment_greedy --save-csv

run-3: $(BUILD)/3_greedy_local_search
	./$(BUILD)/3_greedy_local_search --save-csv

run-3-1: $(BUILD)/3_1_greedy_local_search
	./$(BUILD)/3_1_greedy_local_search --save-csv

run-4: $(BUILD)/4_mmr_phased_local_search
	./$(BUILD)/4_mmr_phased_local_search --save-csv

run-4-1: $(BUILD)/4_1_circular_local_search
	./$(BUILD)/4_1_circular_local_search --save-csv

run-4-2: $(BUILD)/4_2_circular_threshold_local_search
	./$(BUILD)/4_2_circular_threshold_local_search 2 0.01 --save-csv

run-4-3: $(BUILD)/4_3_instrumented_circular_local_search
	./$(BUILD)/4_3_instrumented_circular_local_search 3 0.01 --save-csv

run-4-4: $(BUILD)/4_4_Final_top50
	./$(BUILD)/4_4_Final_top50 --save-csv

run-5: $(BUILD)/5_mmr_forbidden_rebuild
	./$(BUILD)/5_mmr_forbidden_rebuild --save-csv

run-6: $(BUILD)/6_4_1_seeded_string_ga
	./$(BUILD)/6_4_1_seeded_string_ga --save-csv

run-9: $(BUILD)/9_circular_4_cycle
	./$(BUILD)/9_circular_4_cycle --save-csv

run-param: $(BUILD)/parameter_control_heuristic
	./$(BUILD)/parameter_control_heuristic --save-csv

.PHONY: all clean run-1 run-2 run-3 run-3-1 run-4 run-4-1 run-4-2 run-4-3 run-4-4 run-5 run-6 run-9 run-param
