set(CoreServiceInclude /root/RazorCrypto/mht/include)
set(CoreServiceLib /root/RazorCrypto/lib)
set(BinReleaseFolder /root/RazorCrypto/release)

add_compile_options(-Wno-unused-function)
add_subdirectory(mht)
add_subdirectory(api)
add_subdirectory(Tools/MDRunner)
