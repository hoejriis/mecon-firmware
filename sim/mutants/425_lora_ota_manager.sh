# The reference manager for signed updates over LoRa (tools/lora_ota_manager.py, docs/contract/LORA_UPDATES.md sections 6, 7, 11),
# run by the simulator tests against the real firmware: a manager that breaks one of its rules must fail a test.
# it resends frames the answer showed held, not only the missing ones
mutate lora_mgr_resends_held_frames tools/lora_ota_manager.py '            sending = missing if n - 1 in missing else missing + [n - 1]' '            sending = list(range(n))' sim/tests/test_lora_ota_manager.py::test_the_manager_moves_an_image_through_loss_reordering_a_lost_and_a_stale_answer_and_a_strangers_frames
# it ignores bulk_budget_ms and sends into a gateway whose duty window has no room
mutate lora_mgr_ignores_bulk_budget tools/lora_ota_manager.py '            if s["bulk_budget_ms"] >= need and s["queued"] + frames <= 16:' '            if s["queued"] + frames <= 16:' sim/tests/test_lora_ota_manager.py::test_the_manager_never_queues_past_the_gateways_budget
# a resume offers another manifest than the paused job's
mutate lora_mgr_resume_new_manifest tools/lora_ota_manager.py '        again = self.sha256   # the same manifest, named by its hash: a resume is matched by it' '        again = hashlib.sha256((self.text + " ").encode()).hexdigest()' sim/tests/test_lora_ota_manager.py::test_a_pause_at_the_window_then_the_same_manifest_resumes_the_same_job_and_token
# it reads an answer whose base is not the block's: a late answer for an earlier block counts for this one
mutate lora_mgr_accepts_stale_base tools/lora_ota_manager.py '                if a_base != base:   # a late answer to an earlier block is never read against this one' '                if False:   # a late answer to an earlier block is never read against this one' sim/tests/test_lora_ota_manager.py::test_the_manager_moves_an_image_through_loss_reordering_a_lost_and_a_stale_answer_and_a_strangers_frames
