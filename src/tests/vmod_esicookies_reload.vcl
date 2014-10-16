## added by vtc script
#
# import esicookies from "${vmod_topbuild}/src/.libs/libvmod_esicookies.so" ;

sub vcl_fetch {
    if (req.url == "/body2") {
	esicookies.to_esi(beresp.http.Set-Cookie);
    } else {
	set req.http.X-Err = esicookies.to_esi_e(beresp.http.Set-Cookie);
	if (req.http.X-Err && req.http.X-Err != "") {
	    error 503 "Error in to_esi";
	}
    }
    set beresp.do_esi = true;
}

sub vcl_error {
    if (req.http.X-Err) {
	set obj.http.X-Err = req.http.X-Err;
    }
}
