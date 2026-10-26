#ifndef NV_APP_ID_H
#define NV_APP_ID_H
/* Package metadata and the runtime must be built with the same identity. */
#define NV_APP_ID_PRODUCTION "org.marigold.app"   // marigold: own id
#ifndef NV_APP_ID
#define NV_APP_ID NV_APP_ID_PRODUCTION
#define NV_APP_ID_DEFAULT 1
#endif
#ifdef NV_APP_ID_DEFAULT
#define NV_LS_MEDIA_CLIENT "com.webos.media.client.marigold"   // marigold: own name
#else
#define NV_LS_MEDIA_CLIENT "com.webos.media.client." NV_APP_ID
#endif
#endif
