TEST_LIST := timer rx

TEST_BINARIES = $(addprefix $(o)tests/test-,$(TEST_LIST))
ALL_TARGETS += $(TEST_BINARIES)
CLEAN_TARGETS += clean-tests


test_cmd = gtester --verbose $<
test_report_cmd = gtester --verbose -k $< -o $(1)
memleak_cmd = G_DEBUG=gc-friendly G_SLICE=always-malloc valgrind \
		--leak-check=full --track-origins=yes -q $<
memleak_report_cmd = G_DEBUG=gc-friendly G_SLICE=always-malloc valgrind \
		--leak-check=full --track-origins=yes -q --xml=yes --xml-file=$(1) $<

$(o)tests/%.o: tests/%.c
	$(call compile_tgt,tests)

$(o)tests/test-timer: $(o)tests/test-timer.o
	$(call link_tgt,tests)

$(o)tests/test-rx: $(o)tests/test-rx.o $(o)timer.o
	$(call link_tgt,tests)

test-%: $(o)tests/test-%
	$(call test_cmd)

test-report-%: $(o)tests/test-%
	$(call test_report_cmd,$(o)tests/test-report-$*.xml)


test: $(addprefix test-,$(TEST_LIST))
test-report: $(addprefix test-report-,$(TEST_LIST))


clean-tests:
	rm -f $(TEST_BINARIES)
	rm -f $(o)tests/*.o
	rm -f $(o)tests/*.xml
	rm -f $(o)tests/*.gcda
	rm -f $(o)tests/*.gcno
