# marigold-wikidata: the Wikipedia market and station lists the app carries
# (tools/wikidata/wikidata.cpp). CI runs it; the TV never fetches them.
add_executable(marigold-wikidata wikidata.cpp)
marigold_warnings(marigold-wikidata)
target_link_libraries(marigold-wikidata PRIVATE marigold_core)
