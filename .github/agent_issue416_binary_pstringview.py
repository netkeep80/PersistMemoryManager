from pathlib import Path
import re

manager_path = Path("include/pmm/persist_memory_manager.h")
cmake_path = Path("tests/CMakeLists.txt")

manager = manager_path.read_text()

old = r'''static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    if ( s == nullptr )
        s = "";
    detail::relocation_owner<const char, manager_type> source( s );
    auto symbol_policy = pstringview::forest_domain_ops();
    if ( symbol_policy.root_index_ptr() == nullptr )
        return pptr<pstringview>();
    pptr<pstringview> found = symbol_policy.find( s );
    if ( !found.is_null() )
        return found;
    uint32_t len        = static_cast<uint32_t>( std::strlen( s ) );
    size_t   alloc_size = offsetof( pstringview, str ) + static_cast<size_t>( len ) + 1;
    void*    raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();
    s = source.get();
    if ( s == nullptr )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );
    void*             public_raw = raw_user_ptr_from_pptr( new_node );
    if ( public_raw == nullptr )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    std::memcpy( public_raw, &len, sizeof( len ) );
    char* str_dst = static_cast<char*>( public_raw ) + offsetof( pstringview, str );
    std::memcpy( str_dst, s, static_cast<size_t>( len ) + 1 );
    detail::avl_init_node( new_node );
    if ( !lock_block_permanent_unlocked( public_raw ) )
        return pptr<pstringview>();
    symbol_policy.insert( new_node );
    return new_node;
}'''

new = r'''static pptr<pstringview> intern_symbol_unlocked( std::string_view s ) noexcept
{
    constexpr size_t data_offset = offsetof( pstringview, str );
    if ( s.size() > static_cast<size_t>( ( std::numeric_limits<uint32_t>::max )() ) ||
         s.size() > ( std::numeric_limits<size_t>::max )() - data_offset - 1 )
        return pptr<pstringview>();
    const size_t byte_length = s.size();
    const char*  source_ptr  = s.data() != nullptr ? s.data() : "";
    detail::relocation_owner<const char, manager_type> source( source_ptr );
    auto symbol_policy = pstringview::forest_domain_ops();
    if ( symbol_policy.root_index_ptr() == nullptr )
        return pptr<pstringview>();
    pptr<pstringview> found = symbol_policy.find( s );
    if ( !found.is_null() )
        return found;
    const uint32_t len        = static_cast<uint32_t>( byte_length );
    const size_t   alloc_size = data_offset + byte_length + 1;
    void*          raw        = allocate_unlocked( alloc_size );
    if ( raw == nullptr )
        return pptr<pstringview>();
    source_ptr = source.get();
    if ( source_ptr == nullptr && byte_length != 0 )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    pptr<pstringview> new_node   = make_pptr_from_raw<pstringview>( raw );
    void*             public_raw = raw_user_ptr_from_pptr( new_node );
    if ( public_raw == nullptr )
    {
        deallocate_unlocked( raw );
        return pptr<pstringview>();
    }
    std::memcpy( public_raw, &len, sizeof( len ) );
    char* str_dst = static_cast<char*>( public_raw ) + data_offset;
    if ( byte_length != 0 )
        std::memcpy( str_dst, source_ptr, byte_length );
    str_dst[byte_length] = '\0';
    detail::avl_init_node( new_node );
    if ( !lock_block_permanent_unlocked( public_raw ) )
        return pptr<pstringview>();
    symbol_policy.insert( new_node );
    return new_node;
}
static pptr<pstringview> intern_symbol_unlocked( const char* s ) noexcept
{
    return intern_symbol_unlocked( s != nullptr ? std::string_view( s ) : std::string_view{} );
}'''

if manager.count(old) != 1:
    raise SystemExit(f"expected one legacy intern_symbol_unlocked block, got {manager.count(old)}")
manager_path.write_text(manager.replace(old, new))

cmake = cmake_path.read_text()
marker = "pmm_add_test(test_issue410_forest_registry_relocation test_issue410_forest_registry_relocation.cpp)"
line = "pmm_add_test(test_issue416_pstringview_binary test_issue416_pstringview_binary.cpp)"
if cmake.count(marker) != 1 or line in cmake:
    raise SystemExit("unexpected issue410/issue416 CMake registration state")
cmake_path.write_text(
    cmake.replace(marker, marker + "\n\n# Issue 416: length-aware, embedded-NUL-safe pstringview identity\n" + line)
)

constructor = re.compile(r"(\b[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*::pstringview)\s*\(")
roots = [Path("tests"), Path("examples"), Path("demo"), Path("benchmarks")]
replaced = 0
remaining = []
for root in roots:
    if not root.exists():
        continue
    for path in root.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text()
        updated, count = constructor.subn(r"\1::intern(", text)
        if count:
            path.write_text(updated)
            replaced += count
for root in roots:
    if not root.exists():
        continue
    for path in root.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        for match in constructor.finditer(path.read_text()):
            remaining.append(f"{path}:{match.group(0)}")
if replaced == 0:
    raise SystemExit("no legacy pstringview constructor consumers were migrated")
if remaining:
    raise SystemExit("legacy pstringview constructor consumers remain:\n" + "\n".join(remaining))
print(f"migrated {replaced} legacy pstringview constructor call(s)")
