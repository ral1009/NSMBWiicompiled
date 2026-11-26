set(version "v2.0.0-alpha.12")
function(fetch name url digest)
    file(DOWNLOAD "${url}" "${OUT}/${name}.tmp"
        EXPECTED_HASH "SHA256=${digest}" TLS_VERIFY ON)
    file(RENAME "${OUT}/${name}.tmp" "${OUT}/${name}")
endfunction()
fetch(nodtool "https://github.com/encounter/nod/releases/download/${version}/nodtool-linux-x86_64"
    ee8345c004248eb82a86dd0c6068e0fcf3dc75baf68d18fe949bf4589822f5a5)
file(CHMOD "${OUT}/nodtool" PERMISSIONS OWNER_READ OWNER_EXECUTE OWNER_WRITE
    GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
fetch(nodtool-LICENSE "https://raw.githubusercontent.com/encounter/nod/${version}/LICENSE-MIT"
    544ffe9befaefa6b59ab1fdde58789069ba7c1b65432bdea273a16b2117909b7)
