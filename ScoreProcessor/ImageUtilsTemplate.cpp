//#include "stdafx.h"
//#include "ImageUtils.h"
namespace ImageUtils {
	template<typename T>
	bool Rectangle<T>::operator<(Rectangle<T> const& other) const {
		if(left<other.left) return true;
		if(left>other.left) return false;
		return top<other.top;
	}
	template<typename T>
	bool Rectangle<T>::operator>(Rectangle<T> const& other) const {
		if(left>other.left) return true;
		if(left<other.left) return false;
		return top>other.top;
	}
	template<typename T>
	T Rectangle<T>::area() const {
		return (right-left)*(bottom-top);
	}
	template<typename T>
	T Rectangle<T>::perimeter() const {
		return 2*(right-left+bottom-top);
	}
	template<typename T>
	T Rectangle<T>::height() const {
		return bottom-top;
	}
	template<typename T>
	T Rectangle<T>::width() const {
		return right-left;
	}
	template<typename T>
	bool Rectangle<T>::isBorderingVert(Rectangle<T> const& other) const {
		return (other.bottom==top||other.top==bottom)&&overlapsX(other);
	}
	template<typename T>
	bool Rectangle<T>::overlapsX(Rectangle<T> const& other) const {
		return !(left>other.right||right<other.left);
	}
	template<typename T>
	bool Rectangle<T>::isBorderingHoriz(Rectangle<T> const& other) const {
		return (other.left==right||other.right==left)&&overlapsY(other);
	}
	template<typename T>
	bool Rectangle<T>::overlapsY(Rectangle<T> const& other) const {
		return !(top>other.bottom||bottom<other.top);
	}
	template<typename T>
	bool Rectangle<T>::intersects(Rectangle<T> const& other) const {
		return overlapsX(other)&&overlapsY(other);
	}
	template<typename T>
	line<T> Rectangle<T>::diagonal() {
		return {{left,top},{right,bottom}};
	}
	template<typename T>
	std::ostream& operator<<(std::ostream& os,Rectangle<T> const& rect) {
		return os<<"[("<<rect.left<<','<<rect.top<<"),("<<rect.right<<','<<rect.bottom<<")]";
	}
	template<typename U>
	std::ostream& operator<<(::std::ostream& os,line<U> const& aline) {
		return os<<"[("<<aline.a.x<<','<<aline.a.y<<"),("<<aline.b.x<<','<<aline.b.y<<")]";
	}
	template<typename T>
	void compress_rectangles(std::vector<Rectangle<T>,std::allocator<Rectangle<T>>>& container) {
		sort(container.begin(),container.end(),[](Rectangle<T>& a,Rectangle<T>& b) {return a<b;});
		for(unsigned int i=1;i<container.size();++i) {
			if(container[i].left==container[i-1].left&&
				container[i].top==container[i-1].bottom&&
				container[i].right==container[i-1].right) {
				Rectangle<T> temp=container[--i];
				container[i]=Rectangle<T>{temp.left,temp.right,temp.top,container[i+1].bottom};
				container.erase(container.begin()+i+1);
			}
		}
	}

	template<typename T>
	vec2_t<T> vec2_t<T>::operator+(vec2_t<T> const& other) {
		return {x+other.x,y+other.y};
	}
	template<typename T>
	vec2_t<T> vec2_t<T>::operator-(vec2_t<T> const& other) {
		return {x-other.x,y-other.y};
	}
	template<typename T>
	vec2_t<T>& vec2_t<T>::operator+=(vec2_t<T> const& other) {
		x+=other.x;
		y+=other.y;
		return *this;
	}
	template<typename T>
	vec2_t<T>& vec2_t<T>::operator-=(vec2_t<T> const& other) {
		x-=other.y;
		y-=other.y;
		return *this;
	}
	template<typename T>
	void split_horiz(Rectangle<T>* orig,Rectangle<T>* buffer,unsigned int numRects) {
		int height=(orig->bottom-orig->top)/numRects;
		--numRects;
		for(int i=0;i<numRects;) {
			buffer[i]={orig->left,orig->right,orig->top+height*i,orig->top+height*(++i)};
		}
		buffer[numRects]={orig->left,orig->right,orig->top+height*numRects,orig->bottom};
	}
}