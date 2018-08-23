var CACHE = 'myMPD-cache-v4.0.0';
var urlsToCache = [
    '/',
    '/player.html',
    '/css/bootstrap.min.css',
    '/css/mympd.css',
    '/js/bootstrap-native-v4.min.js',
    '/js/mympd.js',
    '/js/player.js',
    '/assets/appicon-167.png',
    '/assets/appicon-192.png',
    '/assets/appicon-512.png',
    '/assets/coverimage-httpstream.png',
    '/assets/coverimage-notavailable.png',
    '/assets/favicon.ico',
    '/assets/MaterialIcons-Regular.eot',
    '/assets/MaterialIcons-Regular.ttf',
    '/assets/MaterialIcons-Regular.woff',
    '/assets/MaterialIcons-Regular.woff2'
];

self.addEventListener('install', function(event) {
    event.waitUntil(
        caches.open(CACHE).then(function(cache) {
            return cache.addAll(
                urlsToCache.map(url => new Request(url, {credentials: 'same-origin'}))
            );
        })
    );
});

self.addEventListener('fetch', function(event) {
  event.respondWith(
    caches.match(event.request).then(function(response) {
        if (response)
            return response
        else
            return fetch(event.request);
      }
    )
  );    
});

self.addEventListener('activate', function(event) {
    event.waitUntil(
        caches.keys().then(function(cacheNames) {
            return Promise.all(
                cacheNames.map(function(cacheName) {
                    if (cacheName != CACHE)
                        return caches.delete(cacheName);
                })
            );
        })
    );
});
