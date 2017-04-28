/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#if !defined(WIN32APP_BASE_HPP__)
#define WIN32APP_BASE_HPP__

#include <stdint.h>
#include <string>

class app_interface_t {
public:
	virtual void on_init() = 0;
	virtual bool on_resize(uint32_t, uint32_t) = 0;
	virtual void on_update() = 0;
	virtual void on_draw() = 0;
	virtual void on_final() = 0;
	virtual uint32_t get_width() const = 0;
	virtual uint32_t get_height() const = 0;
	virtual const wchar_t* get_title() const = 0;

	virtual void on_key(bool up, uint8_t kcode) = 0;
	virtual ~app_interface_t() {};
};

class appbase_t : public app_interface_t {
	uint32_t width_;
	uint32_t height_;
public:
	appbase_t(uint32_t w, uint32_t h) : width_(w), height_(h) {};
	void on_init(){};
	bool on_resize(uint32_t w, uint32_t h){ return false; };
	void on_update(){};
	void on_draw(){};
	void on_final(){};

	void on_key(bool down, uint8_t kcode){};

	uint32_t get_width() const { return width_; };
	uint32_t get_height() const { return height_; };

	const wchar_t* get_title() const { return L"sample appbase";};
};

class win32_window_proc_t {
	static HWND hwnd_;
public:
	static int run(app_interface_t* app, HINSTANCE, int);
	static HWND get_handle() { return hwnd_; }
protected:
	static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
};

template < typename T >
int runapp(uint32_t w, uint32_t h, HINSTANCE instance, int cmd)
{
	T t(w, h);
	return win32_window_proc_t::run(&t, instance, cmd);
}

#endif
