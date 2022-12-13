function(bundle_zrocks tgt_name bundled_tgt_name)
	message( STATUS "Bundling libraries")

	list(APPEND static_libs "lib${LNAME}_slim.a")

	# These are expected to be available on the system
	list(APPEND system_deps ztl)

	message( STATUS "system_deps(${system_deps})")
	include_directories("${PROJECT_SOURCE_DIR}/../include")

	foreach(dep IN LISTS system_deps)
		set(dep_fname "lib${dep}.a")
		unset(dep_path CACHE)
		find_library(dep_path
			NAMES ${dep_fname}
			HINTS "${PROJECT_SOURCE_DIR}/../build/ztl")
		if ("${dep_path}" STREQUAL "dep_path-NOTFOUND")
			message( STATUS "FAILED: find_library(${dep_fname})")
			set(BUNDLE_LIBS "FAILED" PARENT_SCOPE)
			return()
		endif()

		list(APPEND static_libs "${dep_path}")
	endforeach()

	# For the slim library
	target_link_libraries(${LNAME} ${system_deps} rt numa uuid)

	# For the bundled library
	list(REMOVE_DUPLICATES static_libs)
	foreach(dep_fpath IN LISTS static_libs)
		message( STATUS "adding to bundle: ${dep_fpath}" )
	endforeach()

	set(bundled_tgt_full_name
		${CMAKE_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${tgt_name}${CMAKE_STATIC_LIBRARY_SUFFIX})

	set(ar_script ${CMAKE_BINARY_DIR}/${bundled_tgt_name}.ar)
	set(ar_script_in ${CMAKE_BINARY_DIR}/${bundled_tgt_name}.ar.in)

	# Generate archive script
	file(WRITE ${ar_script_in} "CREATE ${bundled_tgt_full_name}\n" )
	foreach(tgt_path IN LISTS static_libs)
		file(APPEND ${ar_script_in} "ADDLIB ${tgt_path}\n")
	endforeach()
	file(APPEND ${ar_script_in} "SAVE\n")
	file(APPEND ${ar_script_in} "END\n")
	file(GENERATE OUTPUT ${ar_script} INPUT ${ar_script_in})

	set(ar_tool ${CMAKE_AR})
	if (CMAKE_INTERPROCEDURAL_OPTIMIZATION)
		set(ar_tool ${CMAKE_C_COMPILER_AR})
	endif()

	add_custom_command(
		COMMAND ${ar_tool} -M < ${ar_script}
		OUTPUT ${bundled_tgt_full_name}
		COMMENT "Bundling ${bundled_tgt_name}"
		VERBATIM)

	# Create CMAKE target
	add_custom_target(bundling_target ALL DEPENDS ${bundled_tgt_full_name})
	add_dependencies(bundling_target ${tgt_name})

	add_library(${bundled_tgt_name} STATIC IMPORTED)

	set_target_properties(${bundled_tgt_name}
		PROPERTIES
		IMPORTED_LOCATION ${bundled_tgt_full_name}
		INTERFACE_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${tgt_name},INTERFACE_INCLUDE_DIRECTORIES>
		)

	add_dependencies(${bundled_tgt_name} bundling_target)

	install(FILES ${bundled_tgt_full_name} DESTINATION lib COMPONENT dev)
	
	set(BUNDLE_LIBS "SUCCESS" PARENT_SCOPE)

endfunction()
