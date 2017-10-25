function(install_assets TARGET OUTPUT INPUT)
	file(MAKE_DIRECTORY ${OUTPUT})
	add_custom_command(TARGET ${TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${INPUT} ${OUTPUT})
endfunction()

