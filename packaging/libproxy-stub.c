/* Minimal stand-in for libproxy in the AppImage.
 *
 * Qt's network library on Ubuntu links libproxy (and through it curl, GnuTLS, OpenSSL, Kerberos, LDAP …) only to
 * look up the system proxy configuration. Éter never uses a proxy: the remote editing connection goes straight to
 * the playout computer's IP address, and the program sets "no proxy" for the whole application. This stub keeps
 * Qt's dependency satisfied and always answers "direct connection", which keeps the AppImage small.
 *
 * Built by scripts/build_appimage.sh with a version script (LIBPROXY_0.4.16), as the real library is.
 */
#include <stdlib.h>
#include <string.h>

typedef struct { int unused; } pxProxyFactory;
static pxProxyFactory factory;

pxProxyFactory *px_proxy_factory_new(void) { return &factory; }

char **px_proxy_factory_get_proxies(pxProxyFactory *self, const char *url)
{
    (void)self;
    (void)url;
    char **list = calloc(2, sizeof(char *));
    if (list) list[0] = strdup("direct://");
    return list;
}

void px_proxy_factory_free_proxies(char **proxies)
{
    if (!proxies) return;
    for (char **p = proxies; *p; ++p) free(*p);
    free(proxies);
}

void px_proxy_factory_free(pxProxyFactory *self) { (void)self; }
